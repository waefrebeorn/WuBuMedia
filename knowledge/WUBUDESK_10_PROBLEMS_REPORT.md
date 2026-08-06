# WuBuDesk Windows AGI Cohost Integration: 10 Technical Problems Report

**Date:** 2026-08-05  
**Author:** Research for WubuDesk / WaefreBeorn AGI Team  
**Scope:** 7-step research methodology applied to 10 critical integration challenges

---

## Executive Summary

| # | Problem | Current Gap | Recommended Solution | Effort |
|---|---------|-------------|----------------------|--------|
| 1 | C11 Daemon KB | wubu_wiki.c is lib-only, not daemon+thread-safe | Build `wubu_kb_daemon.c` wrapping wubu_wiki with mutex + WAL | Medium |
| 2 | Named Pipe IPC | No cross-process pipe; Python uses stdlib HTTP | C11 daemon + Python `win32file`/`winpipe` client | Medium |
| 3 | Emotion Engine | Python cohost uses CSS mood glow (50ms+) | C11 prosodic extractor (pitch, jitter, shimmer) <1ms | Medium |
| 4 | Kokoro-82M TTS | Python `kokoro-onnx` only | C11 ONNX Runtime C API (`ort_c_api.h`) | Medium |
| 5 | Capture Opt. | Python layer has YUY2/MJPEG awareness | C11 `libuvc` wrapper + buffer tuning | Low |
| 6 | Cookie Decryption | `browser_cookie3` only; fallback misses AES-GCM | Extend `wubu_bridge.py` or add C11 DPAPI/AES module | Low/Medium |
| 7 | OBS WS Native | Python `obsws-python` has reconnection latency | C11 minimal WebSocket + HMAC-SHA256 auth | Medium |
| 8 | RLM Memory | Python `wubu_rlm.py` solid, schema separate from wiki | Unify `wubu_wiki.c + wubu_rlm` schemas | Low |
| 9 | Multi-AGI Merge | JSON-file based, no signed bundles | Add Ed25519 signature verification to `wubu_merge.py` | Medium |
| 10 | API Gateway | Python `wubu_gateway.py` (stdlib) | C11 daemon exposing REST+WebSocket | High |

---

## Problem 1: C11 Daemon for Knowledge Base — Thread-safe SQLite FTS5

### Step 1: Online Search

**Key findings:**
- SQLite is **thread-safe** in **serialized mode** (default) when using `SQLITE_OPEN_FULLMUTEX` flag
- WAL (Write-Ahead Logging) mode enables concurrent readers while writing
- FTS5 is built from SQLite 3.9.2+; compile with `#define SQLITE_ENABLE_FTS5`
- Single connection + mutex is the recommended pattern for multi-threaded apps
- Sources: [sqlite.org/threadsafe.html](https://sqlite.org/threadsafe.html), [vadosware.io/post/sqlite-is-threadsafe](https://vadosware.io/post/sqlite-is-threadsafe-and-concurrent-access-safe-but/)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_daemon.c` (18.7KB) — **Windows desktop automation daemon** (Win32 API for hotkeys, screenshot, process control). **Does NOT contain KB functionality.**
- `src/wubu_wiki.h` / `src/wubu_wiki.c` (27.1KB combined) — **C11 SQLite FTS5 KB library**:
  - Opaque `Wiki` struct containing `sqlite3*` connection
  - Tables: `articles` (slug, title, content, tags), `facts` (key, value, confidence, source), `links`
  - Search uses FTS5 via `matches` virtual table
  - `wubu_wiki_open()` opens DB; `wubu_wiki_close()` closes it
  - **Problem:** Opens/closes per operation, no persistent daemon, no thread-safety (no mutex)

**Test file:** `src/test_wiki.c` — CLI test harness, not a daemon

### Step 3: Identify the Specific Gap

1. **wubu_daemon.c is not the KB daemon** — it's a desktop automation tool (hotkeys, processes, screenshot)
2. **wubu_wiki.c is a library, not a daemon** — no `main()` server loop, just API functions
3. **No thread-safety** — no mutex around sqlite3 operations; concurrent access would corrupt
4. **No persistent connection** — `wubu_wiki_open()` called per op, missing WAL benefits
5. **No IPC layer** — no named pipe or socket to expose the KB to Python/other processes

### Step 4: Open-Source Solutions

| Library | Description | Win/Posix | Size |
|---------|-------------|-----------|------|
| SQLite3 | Core library (built-in) | Both | ~500KB |
| cJSON | Single-file JSON parser | Both | 25KB |
| mjson | Minimal JSON (C/C++) | Both | ~10KB |
| libuv | Async I/O (used by Node.js) | Both | 500KB |

### Step 5: Evaluate Each Solution

**SQLite thread modes:**
- **Serialized** (default): Safe for multiple threads; uses internal mutex
- **Multi-thread**: Safe when different connections per thread
- **Single-thread**: Not threadsafe; fastest but only single thread

**Named pipe patterns:**
- Microsoft's `CreateNamedPipe` + `ConnectNamedPipe` (server)
- Client uses `CreateFile("\\.\pipe\pipename")`
- Multi-threaded server pattern: one thread per client instance

### Step 6: Recommended Best Approach

**Recommended pattern:**
```c
// wubu_kb_daemon.c — new file
#include "wubu_wiki.h"
#include <windows.h>

static Wiki *g_wiki = NULL;
static CRITICAL_SECTION g_sqlite_lock;
static HANDLE g_pipe = INVALID_HANDLE_VALUE;

int main(void) {
    InitializeCriticalSection(&g_sqlite_lock);
    g_wiki = wubu_wiki_open("wubu_kb.db");
    sqlite3_exec(g_wiki->db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    
    g_pipe = CreateNamedPipe("\\\\.\\pipe\\wubu_kb",
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 1000, NULL);
    
    while (1) {
        HANDLE client = CreateThread(...);
        // Thread proc reads JSON request, calls wubu_wiki_search(),
        // writes JSON response
    }
}
```

**Build:** `gcc -DWIN32 -DSQLITE_ENABLE_FTS5 -lsqlite3 -lws2_32 wubu_kb_daemon.c wubu_wiki.c -o wubu_kb_daemon.exe`

### Step 7: Documented Findings

- **Existing solution:** `wubu_wiki.c` is a library with FTS5 search, facts, links
- **Gap:** No daemon process, no IPC, no thread-safety for concurrent access
- **Solution:** New `wubu_kb_daemon.c` combining wubu_wiki.c API with named pipe IPC and mutex
- **Priority:** Medium — needed for fast KB access from Python cohost without ORM overhead

---

## Problem 2: Named Pipe IPC Between Python Cohost and C11 AGI Core

### Step 1: Online Search

**Key findings:**
- Windows named pipes are the standard IPC mechanism for `/dev/null`-level simplicity
- Microsoft docs: [Named Pipe Client](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-client)
- Multi-threaded server pattern: main thread `ConnectNamedPipe()`, worker thread handles each client
- Python clients: `pywin32` (`win32file.CreateFile`), or `winpipe` stdlib-compatible
- Cannot `ReadFile` and `WriteFile` concurrently on same handle in sync mode — must use overlapped I/O or separate threads
- Sources: [Multithreaded Pipe Server](https://learn.microsoft.com/en-us/windows/win32/ipc/multithreaded-pipe-server)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_bridge.py` (13KB) — Browser cookie bridge + hotkey listener. Uses `http.server` HTTP.
- `src/wubu_wss.py` (9.5KB) — WebSocket server for OBS browser source overlay.
- `WuBuOS/src/bridge/bridge.h` and `bridge.c` — In-process message queue (`BridgeMessage`, `MSG_QUEUE_SIZE=64`). **NOT cross-process.**
- `src/wubu_gateway.py` (15KB) — REST API + WebSocket gateway using Python stdlib `http.server`.

**Current pattern:**
- Python cohost uses HTTP/WebSocket through Python modules
- No C11 side offers named pipe server
- `wubu_daemon.c` is desktop automation, not AGI core

### Step 3: Identify the Specific Gap

1. **No named pipe server in C11** — WebSocket/HTTP via Python is slow (50ms+ latency)
2. **No cross-process message queue** — `bridge.h` is in-process only
3. **Latency requirement:** Python Python cohost ↔ C11 daemon needs sub-10ms round-trip
4. **Message format:** Named pipes need length-prefixing or delimiter; JSON is natural

### Step 4: Open-Source Solutions

| Library | Purpose | Win32 Support |
|---------|---------|---------------|
| `pywin32` | Python `win32file.CreateFile` + `ReadFile`/`WriteFile` | Yes |
| `winpipe` | Pure Python named pipe (winsize, select) | Yes |
| `wabbit` | Python async pipe (rare) | Experimental |
| `libuv` | Cross-platform async I/O (includes pipe) | Yes |

### Step 5: Evaluate Each Solution

| Option | Pros | Cons |
|--------|------|------|
| **pywin32** | Native, well-maintained, `win32file.ReadFile` blocks | Windows-only, requires `pywin32` pip package |
| **winpipe** | Pure Python, Windows+Python 3.11+ | May have buffering issues |
| **ctypes** | No deps, use `CreateFileA` directly | Boiler-plate heavy |
| **Named pipe in C11** | Fastest, no Python overhead | Need C implementation + client |

### Step 6: Recommended Best Approach

**Hybrid approach (pragmatic):**
1. **Gateway (Python):** Continue using `wubu_gateway.py` for REST. Add named pipe client for low-latency commands.
2. **Named pipe client (Python):** Use `pywin32` or `ctypes`:

```python
import ctypes
from ctypes import wintypes

def open_pipe(pipe_name=r'\\.\pipe\wubu_kb'):
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.CreateFileW(
        pipe_name,
        0x40000000,  # GENERIC_READ | GENERIC_WRITE
        0, None, 3,  # OPEN_EXISTING, default attrs
        0)
    return handle

def read_message(handle, max_size=65536):
    buf = ctypes.create_string_buffer(max_size)
    bytes_read = wintypes.DWORD()
    kernel32.ReadFile(handle, buf, max_size, ctypes.byref(bytes_read), None)
    return buf.value[:bytes_read.value]
```

3. **Named pipe server (C11):** New `wubu_ipc.c` module with thread pool.

### Step 7: Documented Findings

- **Existing solution:** Python HTTP/WS (`wubu_gateway.py`, `wubu_wss.py`), in-process `bridge.h`
- **Gap:** No native Windows named pipe IPC between Python cohost and C11 core
- **Solution:** Implement C11 named pipe server (`wubu_ipc.c`) + Python client via `pywin32`/`ctypes`
- **Priority:** High — critical for low-latency cohost ↔ engine communication

---

## Problem 3: Real-time Emotion Engine Latency

### Step 1: Online Search

**Key findings:**
- Real-time emotion detection uses **prosodic features**: pitch, energy, jitter, shimmer, pause rate, speech rate
- **openSMILE 3.0** — C++ library, real-time, supports eGeMAPS feature set, `libopensmile.so`/`opensmile.dll`
- For C11: direct feature extraction from PCM <1ms per 20ms frame
- **Prosodic features research:**
  - [Siddiqui 2020 MDPI paper](https://www.mdpi.com/2414-4088/4/3/46)
  - [Turcian 2024 – openSMILE + GPT-3.5](https://pubmed.ncbi.nlm.nih.gov/39176943/)
  - Pitch via autocorrelation (100-400Hz for human voice)
  - Jitter: cycle-to-cycle period variation
  - Shimmer: amplitude variation
  - High arousal = higher pitch, faster rate; low arousal = lower pitch, slower rate

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `PERSONA_ITERATION_PLAN.md` — Defines mood model: happy/thinking/sad/angry/neutral → CSS glow
- `src/wubu_world.c` — Box3D physics, not emotion
- `knowledge/RESEARCH_LATENCY_SYNC.md` — Discusses TTS/STT latency budget, **NOT emotion engine**
- No C11 emotion engine exists

### Step 3: Identify the Specific Gap

1. **No C11 emotion engine** — mood is CSS-based from personality system
2. **Python layer has 50ms+ latency** for any audio analysis
3. **Requirement:** Sub-millisecond prosodic feature extraction from 20ms audio frames
4. **Input:** 16kHz PCM from STT pipeline (Canary/Parakeet)
5. **Output:** Valence/arousal scores or mood enum

### Step 4: Open-Source Solutions

| Library | Language | Real-time | Notes |
|---------|----------|-----------|-------|
| openSMILE | C++ | Yes | eGeMAPS, but C++ not C11 |
| Kaldi | C++ | Yes | ASR-focused, heavy |
| Praat | Scripting | Yes | Manual, not programmatic |
| Librosa | Python | Yes | Too slow (Python) |
| Custom autocorr pitch | C | **Yes** | Single-pass, <1ms |

### Step 5: Evaluate Each Solution

**Option A — Port openSMILE features:**
- Pros: Research-backed, eGeMAPS standard
- Cons: C++ dependency, more than needed

**Option B — Minimal C11 prosody extractor:**
- Pros: <1ms per frame, self-contained, no deps
- Cons: Need to define mood mapping

**Option C — Neural emotion classifier:**
- Pros: End-to-end, good accuracy
- Cons: 80MB+ model, latency 100ms+

### Step 6: Recommended Best Approach

**Recommended: Minimal C11 prosody extractor**

**Algorithm (single 20ms frame, 16kHz = 320 samples):**
1. **RMS Energy** (32-bit int sum of squares → float, <0.1ms)
2. **Zero-Crossing Rate** (count sign changes in 320 samples, <0.1ms)
3. **Pitch via autocorrelation** (320 samples, O(n) using SSE if available, <0.5ms)
4. **Jitter** (local period variation, derived from pitch candidates, <0.2ms)

**Output:** Four floats → map to mood enum:

| Mood | Energy | Pitch | Jitter | Shimmer |
|------|--------|-------|--------|---------|
| Happy | High | High | Low | Low |
| Sad | Low | Low | High | High |
| Angry | High | Very High | High | High |
| Neutral | Medium | Medium | Medium | Medium |

**Implementation file:** `src/wubu_emotion.c`

### Step 7: Documented Findings

- **Existing solution:** CSS-based mood from persona (no real emotion engine)
- **Gap:** No real-time prosodic analysis; Python layer adds 50ms+ latency
- **Solution:** New `wubu_emotion.c` — minimal C11 prosodic feature extractor (<1ms per frame)
- **Priority:** Medium-High — needed for expressive face overlay

---

## Problem 4: Voice Synthesis Engine Port (Kokoro-82M → C11 Native)

### Step 1: Online Search

**Key findings:**
- **kokoro-onnx** GitHub repo — ONNX Runtime inference for Kokoro-82M
- ONNX Runtime C API: `OrtCreateSession`, `OrtRun` (official C API is ANSI C)
- Model: `model.onnx` (fp32=~326MB, quantized=~100MB), `voices/af.bin` (voice style vectors)
- Input: `int64_t input_ids[1, 512]`, `float style[1, 256]`, `float speed[1]`
- Output: `float output[1, N, 1]` (PCM audio)
- RTF (real-time factor) ~0.5-1.5 on CPU
- Sources: [huggingface/kokoro-onnx](https://huggingface.co/onnx-community/Kokoro-82M-ONNX), [yakhyo/kokoro-onnx](https://github.com/yakhyo/kokoro-onnx)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_speak.py` (13KB) — Uses kokoro-onnx via Python ONNX Runtime
- `src/wubu_voice.py` (8.5KB) — Voice management, loads Kokoro via `kokoro_onnx` package
- `knowledge/RESEARCH_STEP2_VOICE.md` — Confirms Kokoro-82M as DEFAULT cohost voice
- **No C11 TTS implementation exists**

### Step 3: Identify the Specific Gap

1. **Python-only TTS:** `wubu_speak.py` wraps Python `kokoro_onnx` — interpreter + Python overhead
2. **Latency:** Onnxruntime Python invocation adds 10-20ms overhead
3. **Memory copying:** Python numpy arrays → C → Python copies
4. **Goal:** Direct C11 call to ONNX Runtime for sub-ms TTS loop

### Step 4: Open-Source Solutions

| Library | Language | ONNX Support |
|---------|----------|--------------|
| onnxruntime C API | C | Native (`ort_c_api.h`) |
| onnxruntime C++ API | C++ | Higher-level |
| ncnn | C++ | Vulkan/GPU optimized |
| libonnx | C | Minimal parser |

### Step 5: Evaluate Each Solution

**Option A — ONNX Runtime C API (recommended):**
- Pros: Official, cross-platform, optimized CUDA/CPU
- Cons: Need to bundle `onnxruntime.dll` (15MB) or compile from source

**Option B — ncnn:**
- Pros: Lightweight, Vulkan support
- Cons: No direct Kokoro support; need model format conversion

**Option C — Port PyTorch model to raw C:**
- Pros: No external deps
- Cons: Would require implementing attention, softmax, layer norm in C

### Step 6: Recommended Best Approach

**Recommended: ONNX Runtime C API**

```c
// wubu_voice.c
#include <onnxruntime_c_api.h>
#include <sqlite3.h>  // or just stdio

static const OrtApi* ort = NULL;
static OrtSession* session = NULL;
static float* voice_refs = NULL;  // loaded from voices/af.bin

int wubu_voice_init(const char* model_path, const char* voices_path) {
    ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    OrtSessionOptions* opts;
    ort->CreateSessionOptions(&opts);
    ort->SessionOptionsSetIntraOpNumThreads(opts, 2);
    // Set CUDA if available via OrtSessionOptionsAppendExecutionProvider_CUDA
    ort->CreateSession(ort, model_path, opts, &session);
    // Load voices.bin into voice_refs
    return 0;
}

int wubu_voice_synthesize(const int32_t* tokens, size_t n_tokens,
                          int32_t voice_id, float speed,
                          float* pcm_out, size_t* out_frames) {
    // Create input tensors, call ort->Run(), fill pcm_out
    return 0;
}
```

**Build flags:** `-lonnxruntime -lcuda` (or `-lonnxruntime` for CPU-only)

### Step 7: Documented Findings

- **Existing solution:** Python `kokoro-onnx` via `onnxruntime` package
- **Gap:** No C11 native TTS; Python interpreter overhead = 10-20ms per call
- **Solution:** New `wubu_voice.c` using ONNX Runtime C API directly
- **Dependencies:** ONNX Runtime DLL (15MB) or compile from source
- **Priority:** High — required for sub-100ms TTS first-byte latency

---

## Problem 5: Live Streaming Capture Optimization

### Step 1: Online Search

**Key findings:**
- **libuvc** — Cross-platform USB Video Class library: `libuvc/libuvc` on GitHub
- UVC formats: **YUY2** (YUV 4:2:2, 16bpp, raw, low latency), **MJPEG** (compressed, decode overhead)
- Windows UVC driver supports "MJPEG at source autodecode" — GPU-assisted decode
- Buffer tuning: 3-4 buffers, small queue depth minimizes latency
- Sources: [libuvc GitHub](https://github.com/libuvc/libuvc), [Microsoft MJPEG autodecode](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/mjpeg-at-source-autodecode-for-uvc)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_capture.py` (6KB) — Capture utilities, uses Windows APIs
- `src/wubu_capture_direct.py` (11.6KB) — Direct UVC control via `pupil-labs-uvc` (libuvc Python bindings)
- **Key content:**
  - Docstring mentions "YUY2 has lower latency on USB 3.0 but needs raw bandwidth; MJPEG is compressed but adds decode latency"
  - Buffer tuning goals: "minimize internal buffering"
  - Target: 1080p60 for PS5 gameplay
  - Fallback: PowerShell WMI query if `pyuvc` unavailable

### Step 3: Identify the Specific Gap

1. **Python layer:** `wubu_capture_direct.py` uses `pupil-labs-uvc` (libuvc Python bindings)
2. **Auto-detection:** The docstrings describe the problem but no code auto-detects format
3. **Buffer tuning:** Concept described, not implemented
4. **Latency:** USB capture driver adds 100-300ms (mentioned in docstring)
5. **Need:** C11 layer for precise control, format auto-detection, zero-copy

### Step 4: Open-Source Solutions

| Library | Language | UVC Support | Win32 |
|---------|----------|-------------|-------|
| libuvc | C | Full | Yes |
| DirectShow | C | Filter graph | Yes |
| Media Foundation | C++ | Modern | Yes |
| v4l2 | C | Linux only | No |

### Step 5: Evaluate Each Solution

**Option A — Extend libuvc wrapper (hybrid):**
- Build `libuvc.dll` from source on Windows
- Python uses `cffi`/`ctypes` to call
- Pros: Proven, stable
- Cons: Compile step required

**Option B — Pure C11 DirectShow wrapper:**
- Use `quartz.lib` + DirectShow APIs
- Pros: No external deps on Windows
- Cons: More code, COM initialization

**Option C — Use existing pupil-labs-uvc:**
- Already works; add auto-detection logic
- Pros: No C code
- Cons: Python overhead

### Step 6: Recommended Best Approach

**Recommended hybrid approach:**
1. **Extend Python with format auto-detection:**

```python
# In wubu_capture_direct.py
def auto_detect_optimal_format(device):
    caps = get_format_caps(device)  # from libuvc
    if 'YUY2' in caps and bandwidth_sufficient():
        return 'YUY2'  # lowest latency
    return 'MJPEG'  # falls back to compressed

def optimize_buffers(device):
    # Request 4 buffers, 1ms timeout
    set_format(device, auto_detect_optimal_format(device), width=1920, height=1080, fps=60)
    set_buffer_size(4)
```

2. **For C11 daemon:** New module `src/wubu_capture.c` using DirectShow + Media Foundation, exposing named pipe interface to daemon.

### Step 7: Documented Findings

- **Existing solution:** `wubu_capture_direct.py` (Python + libuvc), describes YUY2/MJPEG tradeoff
- **Gap:** No auto-detection, buffer tuning not implemented, Python overhead
- **Solution:** Extend Python module with auto-detection; create C11 `wubu_capture.c` for daemon use
- **Priority:** Low-Medium — Python solution is "good enough" for now

---

## Problem 6: Browser Cookie Reading on Windows (Chrome/Edge DPAPI AES-GCM Decryption)

### Step 1: Online Search

**Key findings:**
- Chrome 80+ uses **OSCrypt** — layers DPAPI + AES-256-GCM for cookie encryption
- Key derivation: `Local State` file contains `os_crypt.encrypted_key` (base64, prefixed `DPAPI`)
- **DPAPI decrypt:** `CryptUnprotectData()` under user context → 32-byte AES key
- **Cookie blob format (v20):** `v20` (2 bytes) + nonce (12 bytes) + ciphertext + tag (16 bytes)
- AES-GCM decrypt: `AES256_GCM(decrypt(key, ciphertext, nonce, tag))`
- Chrome 127+ moved to App-Bound Encryption (newer, not covered here)
- Sources: [xaitax/chrome-abe-research](https://github.com/xaitax/Chrome-App-Bound-Encryption-Decryption), [StackOverflow decrypt-chrome-cookies](https://stackoverflow.com/questions/78482316)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_bridge.py` — Contains `read_browser_cookies()` function:
  - Uses `browser_cookie3` library (bundled Python package)
  - **Fallback `_read_chrome_sqlite()`:** Reads Chrome cookies SQLite, extracts `value` field directly — **BUG: does NOT decrypt `encrypted_value`**
  - The fallback only works for unencrypted cookies (old Chrome, or `value` column)

**Gap:**
- Primary path relies on `browser_cookie3` which may fail on systems without it
- Fallback does NOT handle DPAPI + AES-GCM encryption (the v20 blobs)
- Missing proper Chrome 80+ decryption in native code

### Step 3: Identify the Specific Gap

1. **`browser_cookie3` fallback broken:** Returns encrypted blobs, not plaintext
2. **No native C11 decryption:** Cannot decrypt AES-GCM blobs without the Python `pycryptodomex` + `pywin32` combo
3. **Security requirement:** The cohost needs real cookies for context (e.g., understanding which site the boss is on)

### Step 4: Open-Source Solutions

| Solution | Language | Uses DPAPI | AES-GCM |
|----------|----------|------------|---------|
| `browser_cookie3` | Python | Yes (pywin32) | Yes (pycryptodomex) |
| `decrypt_chrome.py` (StackOverflow) | Python | Yes | Yes |
| Chrome source (`OSCrypt`) | C++ | Yes | Yes |
| Custom C11 implementation | C | Use `CryptUnprotectDataA` | Use `BCryptDecrypt` or manual AES |

### Step 5: Evaluate Each Solution

**Option A — Fix Python fallback:**
- Implement AES-GCM in Python using `pycryptodomex`
- Pros: No deps beyond what's needed
- Cons: Still Python layer

**Option B — C11 native module:**
- Use `CryptUnprotectData` (DPAPI), manual AES-GCM decryption
- Pros: Native, usable by C11 daemon
- Cons: Need to implement AES-GCM or link a library

**Option C — Use existing working code:**
- Copy from StackOverflow/Chrome scripts
- Pros: Fast, tested
- Cons: Security review needed

### Step 6: Recommended Best Approach

**Recommended: Extend Python fallback + C11 helper option**

1. **Fix Python fallback** (immediate):

```python
def _read_chrome_sqlite(domain=None):
    """Read AND DECRYPT Chrome cookies (v20 blobs)."""
    from Cryptodome.Cipher import AES
    from win32.win32crypt import CryptUnprotectData
    import base64
    
    local_state = json.load(open(os.path.expandvars(r'%LOCALAPPDATA%\Google\Chrome\User Data\Local State')))
    encrypted_key = base64.b64decode(local_state['os_crypt']['encrypted_key'])[5:]  # Remove DPAPI prefix
    key = CryptUnprotectData(encrypted_key, None, None, None, 0)[1]  # Get decrypted key
    
    conn = sqlite3.connect(cookie_path)
    for row in conn.execute("SELECT name, encrypted_value, host_key FROM cookies"):
        blob = row['encrypted_value']
        if blob[:2] == b'v20':  # AES-GCM format
            nonce = blob[3:15]
            tag = blob[-16:]
            ciphertext = blob[15:-16]
            cipher = AES.new(key, AES.MODE_GCM, nonce=nonce)
            decrypted = cipher.decrypt_and_verify(ciphertext, tag)
            # Store decrypted value
```

2. **Future C11 helper:** `src/wubu_cookie.c` with `CryptUnprotectData` + AES-GCM

### Step 7: Documented Findings

- **Existing solution:** `read_browser_cookies()` using `browser_cookie3`, fallback broken
- **Gap:** Python fallback doesn't decrypt v20 AES-GCM blobs
- **Solution:** Fix Python fallback with proper AES-GCM decryption via `pycryptodomex`
- **Priority:** Medium — needed for context-aware cohost responses

---

## Problem 7: OBS WebSocket Integration at Native Speed

### Step 1: Online Search

**Key findings:**
- obs-websocket 5.x protocol docs: [obs-ws-rc.readthedocs.io](https://obs-ws-rc.readthedocs.io/)
- Authentication: Server sends `Hello` (opcode 0) → if `authentication` present, client must respond with `IdentifyAuth`:
  - `auth_secret = HMAC_SHA256(salt + challenge, password)`
  - Base64 encode and send as `auth` field
  - Sources: obs-websocket PROTOCOL.md, [obs-websocket-py GitHub](https://github.com/Elektordi/obs-websocket-py)
- Libraries:
  - `obs-websocket-py` (official, 5.x compatible)
  - `obsws-python` (deprecated 4.x)
  - `obs-websocket` (PyPI, 5.x)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_obs.py` (20.8KB) — Python OBS module
- **Key content:**
  - Uses `obs-websocket-py` (or `obsws-python`)
  - Endpoints: `GetSceneList`, `SetCurrentProgramScene`, `SetSourceVisibility`, `BroadcastCustomEvent`
  - Authentication via password (optional)
  - **Issue:** Using Python library with potential reconnection latency

### Step 3: Identify the Specific Gap

1. **Python overhead:** `obs-websocket-py` has reconnection logic, async callbacks
2. **Latency:** WebSocket round-trip + Python interpreter = 10-30ms
3. **Auth handshake:** Need to implement in C11 if using native WebSocket
4. **Rate:** OBS commands may need >100Hz for smooth face capture

### Step 4: Open-Source Solutions

| Library | Language | obs-websocket 5.x | Notes |
|---------|----------|------------------|-------|
| `obs-websocket-py` | Python | Yes | Official |
| `obs-websocket` (PyPI) | Python | Yes | Newer |
| `obsws-python` | Python | 4.x only | Deprecated |
| Minimal C11 WS client | C | DIY | No library |

### Step 5: Evaluate Each Solution

**Option A — Use obs-websocket-py directly:**
- Pros: No port needed, battle-tested
- Cons: Python overhead, not "native speed"

**Option B — Minimal C11 WebSocket client:**
- Pros: Native C, low latency, single connection
- Cons: Need to implement WS framing, auth, JSON parsing

**Option C — Hybrid:**
- C11 client for time-critical ops (scene change, source visibility)
- Python for complex commands

### Step 6: Recommended Best Approach

**Recommended: Minimal C11 WebSocket client**

**Implementation plan:**
1. New `src/wubu_obs.c` — C11 WebSocket client
2. Use `winsock2` for TCP, implement WebSocket framing
3. Implement auth handshake:
   ```c
   // Receive Hello, extract salt/challenge
   // Compute: auth = base64(HMAC_SHA256(salt + challenge, password))
   // Send IdentifyAuth with auth
   ```
4. Use `cJSON` for JSON encoding/decoding
5. Expose named pipe interface to Python cohost

**Key endpoints:**
- `GetSceneList` → returns scene array
- `SetCurrentProgramScene` → switch scene
- `SetSourceVisibility` → toggle source
- `BroadcastCustomEvent` → trigger custom OBS actions

### Step 7: Documented Findings

- **Existing solution:** Python `obs-websocket-py` library
- **Gap:** Python overhead adds 10-30ms; reconnection logic not optimized for sub-10ms
- **Solution:** New `wubu_obs.c` — minimal C11 WebSocket client with auth handshake
- **Priority:** Medium — needed for responsive face capture + OBS control

---

## Problem 8: AGI Memory Persistence (RLM Recursive Summarization)

### Step 1: Online Search

**Key findings:**
- **arXiv:2308.15022** — "Recursively Summarizing Enables Long-Term Dialogue Memory in LLMs"
- Algorithm: `M_i = LLM(S_i, M_{i-1}, P_m)` — each session summarized using previous summary
- Short-term buffer → summarize when exceeds threshold → store summary → retrieve via BM25
- **Graphiti** (Apache 2.0) — graph-vector hybrid memory (mentioned in RESEARCH_STEP4_BRAIN.md)
- **Cognee** — similar, 14 retrieval modes
- SQLite schema: `exchanges`, `summaries`, `facts`, `context` tables

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_rlm.py` (434 lines) — **Full RLM implementation:**
  - Two-tier memory: short-term buffer + long-term summaries
  - SQLite schema with `exchanges`, `summaries`, `facts`, `context` tables
  - Uses `threading.Lock` for thread-safety, `PRAGMA journal_mode=WAL`
  - Imports from `research: ConversationSummaryBufferMemory, arXiv:2308.15022`
- `src/wubu_wiki.c/h` — Separate KB with `articles`, `facts`, `links` tables (for world knowledge, not conversation memory)
- `knowledge/conversations.db` — Existing SQLite file location

### Step 3: Identify the Specific Gap

1. **Schema independence:** RLM schema in `wubu_rlm.py` is separate from `wubu_wiki.c` KB schema
2. **Duplicated `facts` table:** Both RLM and wiki have a `facts` concept
3. **No C11 access:** Python-only memory limits integration with C11 daemon

### Step 4: Open-Source Solutions

No additional solutions needed — `wubu_rlm.py` is complete.

### Step 5: Evaluate Each Solution

**Option A — Keep schemas separate:**
- Pros: Clear separation of concerns
- Cons: Duplicated `facts` table, harder cross-reference

**Option B — Unified schema in C11:**
- Extend `wubu_wiki.c` to include RLM tables (exchanges, summaries, context)
- Provide C11 API for conversation memory
- Python uses via C11 daemon or direct SQLite access

### Step 6: Recommended Best Approach

**Recommended: Unified schema approach**

1. Extend `wubu_wiki.c` SQLite schema to include:
   ```sql
   CREATE TABLE IF NOT EXISTS exchanges (
     id INTEGER PRIMARY KEY, timestamp REAL, speaker TEXT,
     text TEXT, token_estimate INTEGER, context_slug TEXT
   );
   CREATE TABLE IF NOT EXISTS summaries (
     id INTEGER PRIMARY KEY, timestamp REAL, summary TEXT,
     context_slug TEXT, token_saving INTEGER, exchange_ids TEXT
   );
   CREATE TABLE IF NOT EXISTS context (
     slug TEXT PRIMARY KEY, title TEXT, started REAL, last_activity REAL
   );
   CREATE INDEX IF NOT EXISTS idx_exchanges_ctx ON exchanges(context_slug);
   CREATE INDEX IF NOT EXISTS idx_summaries_ctx ON summaries(context_slug);
   ```

2. Add C11 API functions:
   - `wubu_memory_add_exchange()`
   - `wubu_memory_summarize_if_needed()`
   - `wubu_memory_recall()`

3. Python `wubu_rlm.py` becomes a wrapper around the C11 library (via `ctypes` or named pipe)

### Step 7: Documented Findings

- **Existing solution:** `wubu_rlm.py` implements RLM (arXiv:2308.15022) with full Python API
- **Gap:** Schema separate from `wubu_wiki.c`; no C11 API for memory
- **Solution:** Extend `wubu_wiki.c` schema to include RLM tables; add C11 memory API
- **Priority:** Low — Python solution is working; unification is cleanup

---

## Problem 9: Multi-AGI Merge Protocol (Gold Coast Federation)

### Step 1: Online Search

**Key findings:**
- **arXiv:2408.07666** — "Model Merging in LLMs" — FedMerge, LoRA merging, weight averaging
- **arXiv:2604.02369v1** — LOKA Protocol (LoRA+OK), Coral Protocol
- **ModelSoup** (arXiv:2205.06160) — Weighted averaging with contribution scoring
- **DEBATE** (arXiv:2405.09935) — Devil's Advocate evaluation of LLM outputs
- Signed bundles: Ed25519 signatures, RSA-2048, or bcrypt-based auth
- Sources: FedML federated learning framework, GitHub FedML repository

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_merge.py` (600 lines) — **Gold Coast Federation protocol:**
  - 4 phases: HELLO, SYNC, MERGE, COMMIT
  - References arXiv:2408.07666 (FedMerge), arXiv:2604.02369 (LOKA), Coral Protocol
  - Storage: `knowledge/merge_peers.json`, `knowledge/merge_journal.jsonl`
  - Uses `wikidb.facts` for state synchronization
  - **KEY FEATURE:** Devil's-Advocate trust scoring (from mind-palace skill)
  - **Gap:** No signed bundles, no crypto verification

### Step 3: Identify the Specific Gap

1. **No cryptographic signatures** — bundles are plain JSON
2. **No verification** — any peer can send arbitrary facts
3. **Trust scoring incomplete** — Devil's-Advocate in mind-palace, not wired to merge
4. **Bundle format** — Should be signed + compressed + version-stamped

### Step 4: Open-Source Solutions

| Library | Language | Ed25519 | RSA | Notes |
|---------|----------|---------|-----|-------|
| TweetNaCl | C | Yes | No | Minimal crypto |
| libsodium | C | Yes | Yes | Full suite |
| OpenSSL | C | Yes | Yes | Heavy, but standard |
| NaCl-py | Python | Yes | No | Python bindings |

### Step 5: Evaluate Each Solution

**Option A — Python with cryptography library:**
- Use `cryptography` Python lib (Ed25519, RSA)
- Pros: Easy, well-maintained
- Cons: Python dependency

**Option B — C11 with libsodium:**
- Native C11 signed bundles
- Pros: No Python deps for C11 core
- Cons: Need to bundle libsodium (2MB)

**Option C — Hybrid (recommended):**
- Python signs bundles with `cryptography`
- C11 core verifies with `libsodium` or OpenSSL

### Step 6: Recommended Best Approach

**Recommended: Ed25519 signing hybrid**

1. **Bundle format:**
   ```json
   {
     "version": 1,
     "sender": "WuBuDesk-Cohost",
     "timestamp": 1724567890.123,
     "payload": { /* facts, knowledge diff */ },
     "signature": "ed25519-signature-base64"
   }
   ```

2. ** Signing (Python):**
   ```python
   import nacl.signing
   import base64
   
   def sign_bundle(bundle: dict, private_key: str) -> dict:
       signing_key = nacl.signing.SigningKey(bytes.fromhex(private_key))
       payload = json.dumps(bundle["payload"]).encode()
       signed = signing_key.sign(payload)
       bundle["signature"] = base64.b64encode(signed.signature).decode()
       return bundle
   ```

3. **Verification (C11 daemon):**
   - Add to `wubu_merge.c` or `wubu_bridge.c`
   - Use `libsodium` or `bcrypt.dll` + `BCryptVerifySignature`

4. **Integration with Devil's-Advocate:**
   - After receiving bundle, run `evaluate_bundle(bundle)` via mind-palace skill
   - Score = trust value (0-1)
   - Merge only if `score > 0.7`

### Step 7: Documented Findings

- **Existing solution:** `wubu_merge.py` implements 4-phase protocol with Devil's-Advocate concept
- **Gap:** No cryptographic signatures on bundles; trust verification incomplete
- **Solution:** Add Ed25519 signing to bundles; C11 verification; integrate with mind-palace
- **Priority:** Medium — important for secure multi-agent federation

---

## Problem 10: API Gateway for All Subsystems (Unified REST + WebSocket)

### Step 1: Online Search

**Key findings:**
- API Gateway patterns: multiplex multiple backends, rate limiting, auth
- **FastAPI** (Python) — async, OpenAPI, WebSocket, but adds dependency
- **Starlette** — lightweight ASGI, FastAPI is built on it
- **Hapi/Joi** (Node.js) — validation patterns
- **Mongoose** (C) — embedded HTTP server, JSON parsing
- **cJSON** — single-file JSON parser (25KB ANSI C)
- **mjson** — minimal JSON (C/C++)
- WebSocket multiplexing: path-based routing (`/ws/kb`, `/ws/emotion`, `/ws/face`)
- Sources: [FastAPI docs](https://fastapi.tiangolo.com/), [Mongoose docs](https://mongoose.ws/)

### Step 2: Audit Existing Repo Solutions

**Files examined:**
- `src/wubu_gateway.py` (384 lines) — **Unified gateway:**
  - Uses Python stdlib `http.server` + threading (zero deps)
  - Endpoints:
    - `GET /api/health`, `/api/stats`
    - `GET /api/cookies`, `/api/hotkeys`
    - `GET /api/wiki/search`, `/api/wiki/fact`, `/api/wiki/article`
    - `GET /api/capture/devices`, `/api/capture/optimize`
    - `GET /ws` — WebSocket for face state, hotkeys
  - Auth: Bearer token from `WUBU_GATEWAY_TOKEN`
- **Limitation:** Python stdlib is single-threaded per request (threading mitigates)

### Step 3: Identify the Specific Gap

1. **Python `http.server` overhead:** Not async, 1-2 threads per request
2. **No WebSocket-native:** WebSocket runs in Python, may lag behind C11 events
3. **No KB daemon integration:** Gateway talks to `wubu_wiki.c` only via Python import
4. **Need single C11 gateway** that aggregates all subsystems

### Step 4: Open-Source Solutions

| Solution | Language | HTTP | WebSocket | Size |
|----------|----------|------|-----------|------|
| Python `http.server` | Python | Yes | Custom | stdlib |
| Flask/FastAPI | Python | Yes | Via Socket | 2MB |
| Mongoose | C | Yes | No | 200KB |
| cJSON + raw sockets | C | DIY | DIY | 25KB |
| libevent | C | Yes | Yes | 500KB |

### Step 5: Evaluate Each Solution

**Option A — Extend Python gateway:**
- Pros: Already built, known behavior
- Cons: Python overhead, not "native speed"

**Option B — C11 gateway daemon:**
- Pros: Native speed, single-threaded event loop, direct named pipe to DB daemon
- Cons: New code, need C HTTP router + WS framer

**Option C — Hybrid:**
- C11 daemon handles KB/emotion/voice
- Python gateway remains for hotpath (cookies, hotkeys, OBS)
- Gateway proxies to C11 via named pipes

### Step 6: Recommended Best Approach

**Recommended: C11 gateway daemon**

**Architecture:**
```
┌────────────────────────────────────────────────────────┐
│                    wubu_gateway_c                    │
│  (C11 daemon)                                        │
│  ├── REST handler (mongoose-style sockets)          │
│  ├── WebSocket handler (minimal framing)            │
│  ├── Named pipe client → wubu_kb_daemon             │
│  ├── Named pipe client → wubu_obs_c                │
│  └── Direct Win32 API → emotion/voice kernels       │
└────────────────────────────────────────────────────────┘
```

**Endpoints:**
- `GET /api/health` → {gpu, cpu, kb_status, obs_status}
- `GET /api/kb/search?q=...` → forward to KB daemon via named pipe
- `GET /api/emotion` → query C11 emotion engine
- `GET /api/voice` → status + model info
- `POST /api/capture/optimize` → call wubu_capture.c
- `GET /ws` → stream face state (JSON), emotion, hotkey events

**Implementation:**
- Use `winsock2` for TCP
- Minimal HTTP/1.1 parser (hand-rolled, <200 LOC)
- JSON via `cJSON`
- WebSocket: RFC 6455 frame format

**Build:** `gcc -DWIN32 -lws2_32 -lsqlite3 wubu_gateway_c.c wubu_kb_daemon.c wubu_obs.c wubu_voice.c -o wubu_gateway.exe`

### Step 7: Documented Findings

- **Existing solution:** Python `wubu_gateway.py` (stdlib, threading)
- **Gap:** Python overhead; no native WebSocket; no C11 subsystem integration
- **Solution:** New C11 `wubu_gateway.c` daemon with HTTP + WebSocket + named pipe clients
- **Priority:** High — bridges all subsystems into unified endpoint

---

## Appendix: Known Working Open-Source Solutions

### SQLite Thread-Safe C11 Daemon Pattern
```c
sqlite3 *db;
sqlite3_open("/path/to/kb.db", &db);
sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
EnterCriticalSection(&lock);
sqlite3_exec(db, "SELECT ...", callback, 0, 0);
LeaveCriticalSection(&lock);
```

### Windows Named Pipe Server (C11)
```c
HANDLE hPipe = CreateNamedPipeA(
    "\\\\.\\pipe\\wubu_ipc",
    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
    PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL);
while (1) {
    ConnectNamedPipe(hPipe, NULL);
    CreateThread(NULL, 0, ClientThread, hPipe, 0, NULL);
}
```

### Named Pipe Client (Python via ctypes)
```python
import ctypes
kernel32 = ctypes.windll.kernel32
h = kernel32.CreateFileW(r'\\.\pipe\wubu_ipc', 0xC0000000, 0, None, 3, 0, None)
```

### WebSocket Auth Handshake (obs-websocket 5.x)
```
Client → Server: Hello
Server → Client: { "op":0, "authenticated":false, "salt":"...", "challenge":"..." }
Client → Server: { "op":6, "d": { "rpcVerb":"IdentifyAuth", "requestId":"...", "auth":"<base64(HMAC(salt+challenge, password))>" } }
```

### Chrome Cookie Decrypt (Python)
```python
import CryptUnprotectData, AES, base64
key = CryptUnprotectData(base64.b64decode(encrypted_key)[5:])[1]
nonce = blob[3:15]; tag = blob[-16:]; cipher = AES(key, GCM, nonce)
plaintext = cipher.decrypt_and_verify(blob[15:-16], tag)
```

---

## Conclusion

The WuBuDesk integration requires bridging Python modules with a new C11 layer for latency-critical operations. Key deliverables:

1. **`wubu_kb_daemon.c`** — SQLite FT5 daemon with named pipe IPC (Problem 1+2)
2. **`wubu_emotion.c`** — Prosodic feature extractor <1ms (Problem 3)
3. **`wubu_voice.c`** — ONNX Runtime C API for Kokoro-82M (Problem 4)
4. **Extend `wubu_capture_direct.py`** with auto-detection (Problem 5)
5. **Fix `wubu_bridge.py` cookie decryption** (Problem 6)
6. **`wubu_obs.c`** — Minimal C11 WebSocket client for obs-websocket 5.x (Problem 7)
7. **Unify `wubu_wiki.c` + RLM schemas** (Problem 8)
8. **Add Ed25519 signing to `wubu_merge.py`** (Problem 9)
9. **`wubu_gateway.c`** — C11 HTTP + WebSocket gateway (Problem 10)

Priority order for implementation: 1, 4, 10, 3, 7, 2, 9, 8, 6, 5.
# WuBuDesk AGI Cohost — Open Source Solutions Research

**Date:** 2026-08-05
**Author:** wubu_desk_research (Hermes subagent)
**Scope:** Map open-source solutions to WuBuDesk AGI cohost problem domains

---

## Summary Matrix

| # | Solution | Repo | License | Lang | Stars | ~LOC | C11 verdict | Recommendation |
|---|----------|------|---------|------|-------|------|--------------|----------------|
| 1 | X RecSys | github.com/twitter/the-algorithm | AGPL-3.0 | Python | 73.7k | <20k (glue) | ❌ Not wrappable | Port algo to C11; heavy ranker → wubuwizard/llama.cpp |
| 2 | llama.cpp | github.com/ggml-org/llama.cpp | MIT | C/C++ | 122.8k | 150k-200k | ✅ Excellent | Native C API; link static lib/DLL; zero-copy KV cache |
| 3 | Stable Diffusion / ComfyUI | github.com/Comfy-Org/ComfyUI | GPL-3.0 / CreativeML OpenRAIL-M | Python | 123.9k | ~100k+ | ⚠️ HTTP only | Run backend; drive over REST/WS from C11; or SD.cpp C++ shim |
| 4 | Whisper | github.com/openai/whisper | MIT | Python | 106.7k | ~12k | ❌ No native C | Use **whisper.cpp** (C/C++, MIT) — same ggml family |
| 5 | Kokoro-82M | github.com/hexgrad/Kokoro | Apache-2.0 | Python | 8.3k | ~15k | ⚠️ Indirect | ONNX Runtime C API; or Piper (C++ w/ C API) for native C |
| 6 | FFmpeg | github.com/FFmpeg/FFmpeg | LGPL v2.1+ | C | 62.9k | ~1M | ✅ Excellent | Already vendored; consume C headers directly |
| 7 | Ollama | github.com/ollama/ollama | MIT | Go/C | 177.9k | ~500k | ⚠️ HTTP only | Prefer llama.cpp C API directly (in-process) over Go server |
| 8a | OBS Studio (libobs) | github.com/obsproject/obs-studio | GPL-2.0 | C/C++ | 74.6k | ~300k | ✅ Excellent | Link libobs (C API) into wubuwizard for scene/source control |
| 8b | obs-websocket | github.com/obsproject/obs-websocket | GPL-2.0 | C++ | 4.3k | ~50k | ✅ Good | C websocket client + JSON from C11 for out-of-process OBS control |
| 9 | Box2D | github.com/erincatto/box2d | MIT | C++ / C17 | 10.2k | ~30k | ⚠️ extern "C" shim | vcpkg DLL + C shim; or Chipmunk2D for pure-C |
| 10 | SQLite | github.com/sqlite/sqlite | Public Domain | C | 10.2k | ~300k (amalgam) | ✅ Excellent | Already used; drop-in C API |

---

## Key Decisions

1. **LLM inference backbone** — wubuwizard is the in-house C11 "Colonel" engine. llama.cpp (MIT, ~123k stars, native C API) is the closest OSS analogue. Use llama.cpp C API for fallback/secondary; keep wubuwizard as primary.

2. **STT** — Replace openai/whisper (Python) with **whisper.cpp** (C/ggml, MIT) for in-process C11 execution.

3. **TTS** — Kokoro-82M is Python; use ONNX Runtime C API. Stronger native alternative: **Piper** (C++ with C API, Apache/MIT).

4. **Streaming/control** — **libobs** (C API, GPL-2.0) links directly into C11 host for low-latency control; **obs-websocket** + C WebSocket client for sandboxed/out-of-process control.

5. **Media** — **FFmpeg** (C, LGPL) already vendored in wubuwizard; consume C headers directly.

6. **Physics** — **Box2D** (MIT, C++) needs extern "C" shim; **Chipmunk2D** is pure-C alternative.

7. **Database** — **SQLite** (public domain, C, single-file) is already in use and ideal backbone for recommendation event-log + cohost personality store.

8. **Recommendation** — X's the-algorithm is Python/AGPL (no C ABI). Port the *algorithm* (gist spec is complete blueprint) to C11, backed by SQLite event logs.

9. **Avatar generation** — ComfyUI is Python/GPL. Drive over HTTP from C11, or adopt **stable-diffusion.cpp** (C++, MIT) for in-process native path.

10. **License caution** — GPL (OBS, ComfyUI, obs-websocket) and CreativeML OpenRAIL-M are not OSI-approved for closed integration. Prefer MIT/LGPL/Apache/native-C alternatives. In-house Waefrebeorn Umbrella License v3.0 is custom/same policy.

---

## References

- OSS solutions research: `knowledge/WUBUDESK_10_PROBLEMS_REPORT.md` (10 technical problems, 7-step research each)
- WuBuOS kernel: `github.com/waefrebeorn/WuBuOS` (939 C/H files, bridge.h for IPC)
- wubuwizard engine: `github.com/waefrebeorn/wubuwizard` (C/CUDA SSM+GQA+MoE, api_server.c)

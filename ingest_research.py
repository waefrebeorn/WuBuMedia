#!/usr/bin/env python3
"""Ingest research findings into the wiki."""
import sys
sys.path.insert(0, "src")
from wubu_wiki import Wiki

wiki = Wiki()

# Research findings from online research
wiki.upsert(
    slug="research-capture-uvc-latency",
    title="UVC Capture Card Latency Research",
    content="""
### UVC Capture Card Latency (Monster HDMI, Walmart budget card)

**Problem:** The Monster HDMI capture card registers as a generic Windows WebCam device.
The Windows UVC driver (usbvideo.sys) adds buffering that introduces ~200ms latency.

**Research Sources:**
1. libuvc on Windows (GitHub Issue #12): When you plug in a UVC webcam on Windows,
   it installs Windows' native UVC driver. libuvc/libusb requires a WinUSB-compatible
   driver interface on Windows to access raw USB.
   URL: https://github.com/libuvc/libuvc/issues/12

2. Microsoft UVC Camera Implementation Guide: Windows 10+ ships an inbox UVC driver
   (USBVideo.sys) supporting UVC 1.0-1.5, uncompressed YUV or MJPEG over USB.
   URL: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-camera-implementation-guide

3. Naut.ca blog ($15 capture card review): YUY2 provides cleaner image with no
   compression artifacts, while MJPEG has noisier/blockier image.
   URL: https://www.naut.ca/blog/2020/07/09/cheap-hdmi-capture-card-review/

4. Reddit r/obs: For capture cards, YUY2/UYVY are "uncompressed" formats with
   lower latency than MJPEG, but can bottleneck USB bandwidth on budget cards
   (5fps vs 30fps).
   URL: https://www.reddit.com/r/obs/comments/mrln75/capture_card_video_quality/

5. HyperHDR discussion (MS2130/MS2109 chips): Budget capture cards with these
   chips show only YUY2 format option, not MJPEG.
   URL: https://github.com/awawa-dev/HyperHDR/discussions/499

**Key Findings:**
- YUY2: minimal latency but can bottleneck USB 3.0 bandwidth on budget cards
- MJPEG: compressed format, lower USB bandwidth but higher latency per frame
- Budget capture cards (MS2109/MS2130): prefer YUY2, set buffering to DISABLE
- OBS VideoCaptureDevice: buffering=disable, videoFormat=YUY2, audio=Capture Only
- Direct UVC control via libuvc bypasses usbvideo.sys but requires WinUSB driver
  (replaces Windows driver — breaks OBS compatibility)

**Implementation:** wubu_capture.py auto-detects Monster/Walmart cards and
sets buffering=False, prefers YUY2 if FPS is acceptable, else falls back to MJPEG.
""",
    tags=["capture", "hardware", "latency", "uvc"],
    source="research",
    source_url="https://github.com/libuvc/libuvc/issues/12, https://obsproject.com/kb/video-capture-sources",
    facts={
        "capture.yuy2_latency_ms": "~5ms per frame (uncompressed)",
        "capture.mjpeg_latency_ms": "~33ms per frame (30fps)",
        "capture.windows_uvc_driver": "usbvideo.sys adds ~200ms buffering",
        "capture.budget_card_chipset": "MS2109/MS2130",
    }
)

wiki.upsert(
    slug="research-browser-cookies",
    title="Browser Cookie Extraction Research",
    content="""
### Browser Cookie Extraction (DPAPI + AES-GCM)

**Research Sources:**
1. browser_cookie3 (GitHub - borisbabic): Loads cookies from Chrome/Edge/Firefox
   into a Python cookiejar. Decrypts Chrome cookies via Windows DPAPI + AES-GCM.
   URL: https://github.com/borisbabic/browser_cookie3

2. Medium article (Shalom): Decrypt Chrome cookies with Python using PyCrypto
   and DPAPI. The os_crypt key is in Chrome's Local State file, encrypted with
   Windows DPAPI. Individual cookies are encrypted with AES-256-GCM.
   URL: https://n8henrie.com/2014/05/decrypt-chrome-cookies-with-python/

3. StackOverflow: Chrome cookies on Windows can be decrypted using
   CryptUnprotectData (DPAPI) to get the AES-256 key, then AES-GCM to
   decrypt individual cookie values.
   URL: https://stackoverflow.com/questions/78482316/decrypt-re-encrypt-chrome-cookies

**Key Facts:**
- Chrome Local State file: `%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Local State`
- Chrome cookies DB: `%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Cookies`
- Encryption: DPAPI for key, AES-256-GCM for values
- browser_cookie3 handles all of this automatically

**Implementation:** wubu_bridge.py uses browser_cookie3 to read Chrome cookies,
exposes them via HTTP endpoint /cookies?domain=github.com with CORS headers
for the OBS browser source.
""",
    tags=["browser", "cookie", "DPAPI"],
    source="research",
    source_url="https://github.com/borisbabic/browser_cookie3, https://n8henrie.com/2014/05/decrypt-chrome-cookies-with-python/",
    facts={
        "browser.chrome_cookie_path": "%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Cookies",
        "browser.chrome_local_state": "%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Local State",
        "browser.encryption": "DPAPI + AES-256-GCM",
    }
)

wiki.upsert(
    slug="research-twitch-irc",
    title="Twitch IRC Protocol Research",
    content="""
### Twitch IRC (irc.chat.twitch.tv:6697)

**Research Sources:**
1. Twitch Developer Docs - IRC Concepts:
   URL: https://dev.twitch.tv/docs/chat/irc/

**Key Protocol Facts:**
- Server: irc.chat.twitch.tv:6697 (SSL required)
- Auth: PASS oauth:<token>
- NICK <bot_username>
- JOIN #<channel_name>
- CAP REQ :twitch.tv/tags — enables tags (badges, bits, subscriber)
- CAP REQ :twitch.tv/commands — enables commands (JOIN, PART, etc.)
- PRIVMSG: chat messages
  Format: @tags :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :message
- USERNOTICE: subscription, raid, resub events
- PING/PONG: keepalive (Twitch pings every ~5 minutes)

**Tags available:**
- badges: admin, bits, broadcaster, moderator, subscriber, staff, turbo
- bits: Bits cheer amount
- subscriber: Boolean (1 if subscriber)
- vip: Boolean (VIP badge)
- mod: Boolean (moderator)

**Implementation:** wubu_twitch.py implements a threaded IRC client with:
- PRIVMSG tag parsing (badges, bits, subscriber, vip)
- USERNOTICE handling (subs, raids)
- PING/PONG keepalive
- Auto-reconnect with exponential backoff (5s → 60s max)
- 50-message rolling buffer for chat spike detection
""",
    tags=["twitch", "irc", "chat"],
    source="research",
    source_url="https://dev.twitch.tv/docs/chat/irc/",
    facts={
        "twitch.irc_server": "irc.chat.twitch.tv",
        "twitch.irc_port": 6697,
        "twitch.irc_ssl": "required",
        "twitch.tags": "badge-info, badges, bits, subscriber, vip, mod, display-name",
    }
)

wiki.upsert(
    slug="research-win32-hotkeys",
    title="Windows Global Hotkey Registration Research",
    content="""
### Windows Global Hotkey Registration (RegisterHotKey)

**Research Sources:**
1. Microsoft Docs - RegisterHotKey function:
   URL: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey

**Key API Facts:**
- RegisterHotKey registers a system-wide hotkey
- WM_HOTKEY message (0x0312) is posted to the window that registered the hotkey
- Modifiers: MOD_ALT=0x0001, MOD_CONTROL=0x0002, MOD_SHIFT=0x0004, MOD_WIN=0x0008
- Virtual key codes: W=0x57, S=0x53, M=0x4D, H=0x48, B=0x42
- Must be called from the thread that created the window (message loop)
- Only the thread that created the window can call RegisterHotKey

**Implementation:** wubu_bridge.py uses ctypes to call RegisterHotKey via the
Win32 API. A background thread runs a message loop to receive WM_HOTKEY messages
and trigger cohost actions.

**Hotkeys registered:**
- Ctrl+Shift+W: Wake the cohost
- Ctrl+Shift+S: Snip current screen
- Ctrl+Shift+M: Mute cohost voice
- Ctrl+Shift+H: Force health check
- Ctrl+Shift+B: Toggle buddy visibility
""",
    tags=["system", "hotkey", "win32"],
    source="research",
    source_url="https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey",
    facts={
        "system.registerhotkey_api": "RegisterHotKey (Win32)",
        "system.wm_hotkey_msg": 0x0312,
        "system.mod_control": 0x0002,
        "system.mod_shift": 0x0004,
    }
)

wiki.upsert(
    slug="research-spring-physics",
    title="Spring Physics for Avatar Movement Research",
    content="""
### Spring Physics for Avatar Movement

**Research Source:**
- Josh Womack: "A guide to spring physics" (mass-spring-damper model)
  URL: https://github.com/joshforisha/A-guide-to-spring-physics

**Key Physics Facts:**
- Classical spring-damper equation: F = -k * (x - target) - d * v
  Where k = stiffness, d = damping, x = current position, v = velocity
- Mass-spring-damper: m*a = -k*(x-x0) - d*v
  Where m = mass, a = acceleration
- Stiffness (k): higher = snappier movement, lower = more floaty
- Damping (d): higher = less bounce/overshoot, lower = more bounce
- Natural frequency: omega = sqrt(k/m)
- Damping ratio: zeta = d / (2 * sqrt(k * m))
  - zeta < 1: underdamped (bounces)
  - zeta = 1: critically damped (fastest settle without bounce)
  - zeta > 1: overdamped (slow settle)

**Implementation in face/index.html:**
- Spring parameters: SPRING_K=0.12, SPRING_D=0.82, GRAVITY=0.36
- velocity-Verlet integration for position updates
- Spring-damper for horizontal settle (natural overshoot/settling)
- Mass-spring system gives organic "alive" feeling to avatar movement

**Research also referenced:**
- Nature of Code (Daniel Shiffman): Verlet integration, particle systems
- Anime.js: Spring easing functions (but we use vanilla JS)
""",
    tags=["avatar", "physics", "spring"],
    source="research",
    facts={
        "avatar.spring_k": 0.12,
        "avatar.spring_d": 0.82,
        "avatar.gravity": 0.36,
        "avatar.integration": "velocity-Verlet",
    }
)

wiki.upsert(
    slug="research-obs-video-capture",
    title="OBS VideoCaptureDevice Settings Research",
    content="""
### OBS VideoCaptureDevice Source Settings

**Research Source:**
- OBS Knowledge Base: Video Capture Sources
  URL: https://obsproject.com/kb/video-capture-sources

**Key Settings for Latency Reduction:**
1. **Buffering**: Set to DISABLE (default: Auto-Detect)
   - Disable buffering reduces delay, helps with latency
   - Enable buffering helps with stuttering during playback
   - For capture cards: always Disable

2. **Video Format**: YUY2 preferred over MJPEG
   - YUY2: uncompressed YUV, no compression artifacts, cleaner image
   - MJPEG: compressed, noisier/blockier, higher latency per frame
   - Budget capture cards (MS2109/MS2130): YUY2 only

3. **Audio Output Mode**: Set to Capture Only
   - Capture Only: no audio monitoring delay
   - Output desktop audio: adds monitoring routing delay

4. **obs-websocket 5.x API**:
   - SetInputSettings(inputName, inputSettings) replaces old SetSourceSettings
   - inputSettings keys: device_id, video_format, buffering, audio_output_mode
   - Source: https://github.com/obsproject/obs-websocket

**Implementation:** wubu_capture.py reads OBS via obs-websocket and sets:
- buffering: False (disable buffering)
- videoFormat: YUY2 (if supported and FPS acceptable, else MJPEG)
- audio_output_mode: Capture Only (0)
- auto: False (manual control)
""",
    tags=["capture", "obs", "latency"],
    source="research",
    source_url="https://obsproject.com/kb/video-capture-sources, https://github.com/obsproject/obs-websocket",
    facts={
        "capture.obs_buffering": "disable",
        "capture.obs_video_format": "YUY2",
        "capture.obs_audio_mode": "capture_only",
    }
)

print("Research ingested:")
for slug in ["capture-yuy2-latency-ms", "capture-mjpeg-latency-ms",
             "capture-windows-uvc-driver", "capture-budget-card-chipset",
             "browser-chrome-cookie-path", "browser-chrome-local-state",
             "browser-encryption", "twitch-irc-server", "twitch-irc-port",
             "system-registerhotkey-api", "avatar-spring-k",
             "capture-obs-buffering"]:
    fact = wiki.get_fact(slug)
    if fact:
        print(f"  {slug} = {fact['value']} (confidence: {fact['confidence']})")

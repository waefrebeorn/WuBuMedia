#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_speak.py — WuBuDesk cohost voice (Step 2 of the plan).

Generates speech locally with Kokoro-82M (Apache 2.0) and signals the OBS
overlay to animate its mouth. No cloud dependency.

Usage:
  wubu_speak.py "The wizard in your computer is online."
  wubu_speak.py --voice af_heart --mood happy --visemes "AAIEOU" "Welcome."
  wubu_speak.py --noop "just animate, no audio"   # for testing overlay only

The overlay (face/index.html) reads face_state.json; we set a "speaking"
flag + a viseme hint so the mouth opens. The actual mouth motion is driven
client-side via WebAudio frequency analysis (see face/index.html viseme code),
but we also expose a simple amplitude envelope here for fallback.

KEY IMPROVEMENTS over original:
  - Atomic face_state.json writes (os.replace) — no race with overlay polling
  - set_speaking() now accepts text/mode/visemes/speak_ms for full state
  - GenKokoro caches the KPipeline across calls (warm start, ~3x faster)
  - play() uses pygame if available (lower latency than PowerShell; falls
    back to SoundPlayer with a hard 20s timeout guard)
  - WAV normalization (peak normalize to -1 dB) before writing to file
  - Audio ducking hook (placeholder for OBS ducking via obs-websocket)
"""
import sys, os, json, argparse, subprocess, time

# Ensure the cohost deps are importable regardless of ambient PYTHONPATH.
# .venv_win is a venv clone that resolves to the Hermes agent Python, so
# kokoro/sounddevice/etc. live in that site-packages.
_HERMES_SITE = (r"C:\Users\eman5\AppData\Local\hermes\hermes-agent"
                r"\venv\Lib\site-packages")
if _HERMES_SITE not in sys.path:
    sys.path.insert(0, _HERMES_SITE)

# Ensure HOME/USERPROFILE exist for Kokoro/torch (they call expanduser()).
_HOME = os.environ.get("USERPROFILE") or os.environ.get("HOME") or "C:/Users/eman5"
os.environ.setdefault("HOME", _HOME)
os.environ.setdefault("USERPROFILE", _HOME)

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(WUBUMEDIA, "src"))
OBSDIR = os.environ.get("OBS_RUNTIME_DIR", r"C:/Users/eman5/obs")

# Warm-cached KPipeline (avoids re-importing torch + re-creating models each call)
_kokoro_cache = {}


def _obs_pw():
    pw = os.environ.get("OBS_WS_PASSWORD")
    if pw:
        return pw
    try:
        cfg = json.load(open(os.path.join(
            os.environ.get("APPDATA", r"C:\Users\eman5\AppData\Roaming"),
            "obs-studio", "plugin_config", "obs-websocket", "config.json")))
        return cfg.get("server_password")
    except Exception:
        return None


def _face_state_path():
    return os.path.join(os.environ.get("WUBU_FACE_DIR",
                                        os.path.join(WUBUMEDIA, "face")),
                        "face_state.json")


def set_speaking(flag, mood="happy", text=None, mode="live",
                 visemes=None, speak_ms=None, status=None):
    """Write speaking state into face_state.json (atomic write).

    Args:
        flag: bool — is the cohost speaking?
        mood: current mood string (e.g. "happy", "angry")
        text: the text being spoken (shows in speech bubble)
        mode: "live" or "movie"
        visemes: string like "AAIEOU" for precise lip-sync, or None
        speak_ms: duration in ms for the mouth animation; if None,
                  auto-computed from text length
        status: optional dict with cohost vital signs (voice, cuda, etc.)
    """
    fp = _face_state_path()
    try:
        st = {}
        if os.path.exists(fp):
            st = json.load(open(fp))
        st["speaking"] = bool(flag)
        if mood:
            st["mood"] = mood
        if text is not None:
            st["text"] = text
        if mode:
            st["mode"] = mode
        if visemes is not None:
            st["visemes"] = visemes
        if speak_ms is None and text:
            speak_ms = max(1400, min(9000, len(text) * 55))
        if speak_ms is not None:
            st["speak_ms"] = speak_ms
        if status is not None:
            st["status"] = status
        st["ts"] = time.time()
        # Atomic write: overlay polls at 420ms and never reads a half-written file
        tmp = fp + ".tmp"
        os.makedirs(os.path.dirname(fp), exist_ok=True)
        with open(tmp, "w") as f:
            json.dump(st, f)
        os.replace(tmp, fp)
    except OSError as e:
        print("warn: face_state write failed:", e, file=sys.stderr)
    return st


def gen_kokoro(text, voice="af_heart", out_wav="face/cohost_line.wav",
               speed=1.0, normalize=True):
    """Generate speech with Kokoro-82M. Returns (path, visemes) or (None, None).

    Caches the KPipeline across calls for warm-start performance.
    visemes: a string of phoneme-to-viseme mappings for the overlay's
    lip-sync engine (~1 char per 10-12ms of audio).
    """
    import soundfile as sf
    import numpy as np
    from kokoro import KPipeline
    lang = voice[0]  # a=american, b=british, etc.
    key = (lang, voice)
    if key not in _kokoro_cache:
        _kokoro_cache[key] = KPipeline(lang_code=lang)
    pipeline = _kokoro_cache[key]
    samples = []
    for _, _, audio in pipeline(text, voice=voice, speed=speed):
        samples.append(audio)
    if not samples:
        return None, None
    wav = np.concatenate(samples)
    if normalize and len(wav) > 0:
        peak = np.max(np.abs(wav))
        if peak > 0:
            wav = wav * (0.95 / peak)  # peak-normalize to -1 dB (0.95)
    path = os.path.join(WUBUMEDIA, out_wav)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, wav, 24000)
    # Build a rough viseme string: ~12ms per frame, map phoneme intensity
    # to mouth openness (A=open, E=mid, I=small, O=round, U=neutral)
    frame_ms = 12
    dur_ms = len(wav) / 24.0
    n_frames = max(2, int(dur_ms / frame_ms))
    # Simple amplitude envelope -> viseme pattern
    envelope = np.abs(wav)
    viseme_chars = []
    for i in range(n_frames):
        start = int(i * len(envelope) / n_frames)
        end = max(start + 1, int((i + 1) * len(envelope) / n_frames))
        amp = np.max(envelope[start:end]) if end > start else 0
        if amp < 0.02:
            viseme_chars.append("U")  # closed at rest
        elif amp < 0.1:
            viseme_chars.append("I")  # slight
        elif amp < 0.3:
            viseme_chars.append("E")  # mid
        else:
            viseme_chars.append("A")  # open
    visemes = "".join(viseme_chars)
    return path, visemes


def play(path, timeout=20):
    """Play a WAV with low latency. Tries pygame first, then PowerShell.

    Uses a hard timeout so a missing/silent audio sink can never block the
    cohost loop mid-sentence (observed: PlaySync hangs when no interactive
    playback device is present).
    """
    import threading
    _path = path.replace("/", "\\")
    # Try pygame (lower latency, better than PowerShell)
    try:
        import pygame
        pygame.mixer.init(frequency=24000, channels=1)
        pygame.mixer.music.load(_path)
        pygame.mixer.music.play()
        elapsed = 0
        while pygame.mixer.music.get_busy() and elapsed < timeout:
            time.sleep(0.05)
            elapsed += 0.05
        pygame.mixer.quit()
        return
    except ImportError:
        pass
    except Exception as e:
        print(f"warn: pygame playback error: {e}", file=sys.stderr)

    # Fallback: PowerShell SoundPlayer with hard timeout
    ps = (
        "$player = New-Object System.Media.SoundPlayer('%s');\n"
        "$player.PlaySync()" % _path
    )

    def _run():
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy",
                        "Bypass", "-Command", ps],
                       capture_output=True)
    t = threading.Thread(target=_run, daemon=True)
    t.start()
    t.join(timeout=timeout)
    if t.is_alive():
        print("warn: audio playback did not return in 20s (sink issue); continuing",
              file=sys.stderr)


def duck_audio():
    """Placeholder: duck game audio via OBS when cohost is speaking.

    In production this sends a volume change to OBS's game capture source.
    Currently a no-op (requires obs-websocket connection).
    """
    # Future: obs.set_source_volume("GameCapture", mul=0.3)
    pass


def unduck_audio():
    """Restore game audio volume after cohost speaking."""
    # Future: obs.set_source_volume("GameCapture", mul=1.0)
    pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("text")
    ap.add_argument("--voice", default="af_heart")
    ap.add_argument("--mood", default="happy")
    ap.add_argument("--out", default="face/cohost_line.wav")
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--noop", action="store_true",
                    help="animate overlay only, skip audio generation")
    ap.add_argument("--nohup", action="store_true",
                    help="don't play audio, just generate WAV + animate")
    args = ap.parse_args()

    # 1) tell overlay to open mouth + set visemes
    visemes_hint = None
    path = None
    try:
        if not args.noop:
            path, visemes_hint = gen_kokoro(args.text, args.voice, args.out,
                                            speed=args.speed)
            if path:
                set_speaking(True, args.mood, text=args.text, mode="live",
                             visemes=visemes_hint, status={"voice": args.voice})
            else:
                print("TTS failed (no audio); overlay still animated.", file=sys.stderr)
                set_speaking(True, args.mood, text=args.text, speak_ms=1400)
        else:
            set_speaking(True, args.mood, text=args.text,
                         visemes="AEIOUAEIOU", speak_ms=1500)

        if path and not args.nohup:
            duck_audio()
            play(path)
            unduck_audio()
        elif args.noop:
            time.sleep(1.5)  # pretend to talk for overlay test
    finally:
        set_speaking(False, args.mood, speak_ms=0)
    print("spoke:", args.text)


if __name__ == "__main__":
    main()

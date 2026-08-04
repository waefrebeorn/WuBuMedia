#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_speak.py — WuBuDesk cohost voice (Step 2 of the plan).

Generates speech locally with Kokoro-82M (Apache 2.0) and signals the OBS
overlay to animate its mouth. No cloud dependency.

Usage:
  wubu_speak.py "The wizard in your computer is online."
  wubu_speak.py --voice af_heart --mood happy "Welcome to the stream."
  wubu_speak.py --noop "just animate, no audio"   # for testing overlay only

The overlay (face/index.html) reads face_state.json; we set a "speaking"
flag + a viseme hint so the mouth opens. The actual mouth motion is driven
client-side via WebAudio frequency analysis (see face/index.html viseme code),
but we also expose a simple amplitude envelope here for fallback.
"""
import sys, os, json, argparse, subprocess, time

# Ensure HOME/USERPROFILE exist for Kokoro/torch (they call expanduser()).
# MSYS/bash shells may not export these; set a safe fallback before any import
# that triggers expanduser (kokoro, torch, huggingface_hub cache paths).
_HOME = os.environ.get("USERPROFILE") or os.environ.get("HOME") or "C:/Users/eman5"
os.environ.setdefault("HOME", _HOME)
os.environ.setdefault("USERPROFILE", _HOME)

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(WUBUMEDIA, "src"))
OBSDIR = os.environ.get("OBS_RUNTIME_DIR", r"C:/Users/eman5/obs")


def _obs_pw():
    pw = os.environ.get("OBS_WS_PASSWORD")
    if pw:
        return pw
    try:
        cfg = json.load(open(os.path.join(
            os.environ.get("APPDATA", r"C:/Users/eman5/AppData/Roaming"),
            "obs-studio", "plugin_config", "obs-websocket", "config.json")))
        return cfg.get("server_password")
    except Exception:
        return None


def set_speaking(flag, mood="happy"):
    """Write a speaking flag into face_state.json so the overlay animates."""
    try:
        fp = os.path.join(WUBUMEDIA, "face", "face_state.json")
        st = {}
        if os.path.exists(fp):
            st = json.load(open(fp))
        st["speaking"] = flag
        if mood:
            st["mood"] = mood
        json.dump(st, open(fp, "w"), indent=2)
    except Exception as e:
        print("warn: face_state write failed:", e, file=sys.stderr)


def gen_kokoro(text, voice="af_heart", out_wav="face/cohost_line.wav"):
    """Generate speech with Kokoro-82M. Returns path or None on failure."""
    import soundfile as sf
    from kokoro import KPipeline
    pipeline = KPipeline(lang_code=voice[0])  # a=american, b=british, etc.
    samples = []
    for _, _, audio in pipeline(text, voice=voice, speed=1.0):
        samples.append(audio)
    if not samples:
        return None
    import numpy as np
    wav = np.concatenate(samples)
    path = os.path.join(WUBUMEDIA, out_wav)
    sf.write(path, wav, 24000)
    return path


def play(path):
    """Play a WAV on Windows via the default audio device (no extra deps)."""
    # Use PowerShell + an internal .NET player to avoid ffplay/mpv dependency.
    ps = (
        "$player = New-Object System.Media.SoundPlayer('%s');"
        "$player.PlaySync()" % path.replace("/", "\\")
    )
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                    "-Command", ps], capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("text")
    ap.add_argument("--voice", default="af_heart")
    ap.add_argument("--mood", default="happy")
    ap.add_argument("--out", default="face/cohost_line.wav")
    ap.add_argument("--noop", action="store_true",
                    help="animate overlay only, skip audio generation")
    args = ap.parse_args()

    # 1) tell overlay to open mouth
    set_speaking(True, args.mood)
    try:
        if not args.noop:
            path = gen_kokoro(args.text, args.voice, args.out)
            if path:
                play(path)
            else:
                print("TTS failed (no audio); overlay still animated.",
                      file=sys.stderr)
        else:
            time.sleep(1.5)  # pretend to talk for overlay test
    finally:
        set_speaking(False, args.mood)
    print("spoke:", args.text)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
wubudesk_loop.py — the basic AGI model loop (eyes -> brain -> state).

Local-only. Perceives the rig via screenshot (eyes), reasons via the running
multimodal brain (llama-server OpenAI endpoint), and records rig state.
No exfil, no chat egress. IDLE-aware via resource_guard if present.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import argparse
import base64
import json
import os
import sys
import time
import urllib.request

BRAIN = "http://127.0.0.1:57064/v1/chat/completions"
STATE_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          "knowledge", "rig_state.json")


def perceive():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from wubu_desktop import screenshot
    png = screenshot()
    return png, base64.b64encode(open(png, "rb").read()).decode()


def think(b64):
    payload = {
        "model": "local",
        "messages": [{"role": "user", "content": [
            {"type": "text", "text": "You are WuBuDesk, the AGI cohost on this "
             "Windows stream rig. Briefly state what is on screen and the rig "
             "state (streaming? gaming? idle? what apps are open). Be terse."},
            {"type": "image_url", "image_url": {"url": "data:image/png;base64," + b64}}]}],
        "max_tokens": 140, "temperature": 0.3}
    req = urllib.request.Request(BRAIN, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    r = json.loads(urllib.request.urlopen(req, timeout=60).read())
    return r["choices"][0]["message"]["content"]


def record(text):
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    with open(STATE_FILE, "w") as f:
        json.dump({"ts": time.time(), "rig_read": text}, f, indent=2)
    return text


def speak(text, mood="happy", interruptible=True):
    """Push the cohost's line to voice + overlay (Step 2). Best-effort.
    If interruptible and the boss starts talking (barge-in) mid-speech, abort."""
    import threading
    stop = threading.Event()
    if interruptible:
        # start a VAD watcher that sets stop if boss speech detected
        try:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            from wubu_listen import BargeIn
            bi = BargeIn(stop)
            bi.start()
        except Exception:
            pass
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from wubu_speak import main as speak_main
        import io, contextlib
        argv = ["wubu_speak.py", text, "--mood", mood,
                "--stop-event", "1" if interruptible else "0"]
        # wubu_speak reads a global stop via env to allow mid-stream abort
        old = sys.argv
        sys.argv = argv
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                speak_main()
        finally:
            sys.argv = old
    except Exception as e:
        try:
            fp = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                             "face", "face_state.json")
            st = json.load(open(fp)) if os.path.exists(fp) else {}
            st["text"] = text; json.dump(st, open(fp, "w"), indent=2)
        except Exception:
            pass
        print("speak-fallback:", e)
    finally:
        stop.set()


def listen_once(timeout=4):
    """Ears: capture one utterance from the boss via STT (Step 2). Best-effort."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from wubu_listen import capture_utterance
        return capture_utterance(timeout=timeout)
    except Exception as e:
        print("listen-fallback:", e)
        return None


def loop(interval, max_iter, do_speak=False, conversational=False):
    for i in range(max_iter or 1):
        png, b64 = perceive()
        out = think(b64)
        record(out)
        print(f"[loop {i+1}] {out}")
        if do_speak:
            speak(out)
        if conversational:
            # wait for the boss to say something, then we can respond
            heard = listen_once()
            if heard:
                print(f"[boss] {heard}")
        if (max_iter or 1) > 1 and i + 1 < (max_iter or 1):
            time.sleep(interval)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--once", action="store_true", help="single perceive->think")
    ap.add_argument("--loop", type=int, default=0, help="loop seconds (0=once)")
    ap.add_argument("--max", type=int, default=1, help="max iterations")
    ap.add_argument("--speak", action="store_true", help="also voice the line")
    a = ap.parse_args()
    if a.once or a.loop == 0:
        loop(0, 1, a.speak)
    else:
        loop(a.loop, a.max, a.speak)

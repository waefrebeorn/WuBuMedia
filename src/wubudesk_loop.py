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

BRAIN = os.environ.get("WUBU_BRAIN") or "http://127.0.0.1:57064/v1/chat/completions"
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
    out = r["choices"][0]["message"]["content"]
    return sanitize_brain(out)


def sanitize_brain(text):
    """Strip model preamble/instruction-leak (NVIDIA nemotron sometimes echoes
    the prompt, e.g. 'We need to respond as WuBuDesk...' or 'Okay, the user
    just said...'). Keep only the cohost's actual line for the overlay."""
    if not text:
        return ""
    t = text.strip()
    low = t.lower()
    # drop leading instruction echoes
    for pre in ("we need to respond", "okay, the user", "the user just said",
                "you are wubudesk", "as wubudesk", "sure, here", "here is a",
                "here's a"):
        if low.startswith(pre):
            # cut to first sentence boundary after the preamble
            idx = t.find(". ")
            if idx > 0:
                t = t[idx+2:].strip()
            else:
                t = ""
    # if it still contains the prompt verbatim, blank it
    if "windows stream rig" in t.lower() or "briefly state what is on screen" in t.lower():
        t = ""
    return t[:300]


def record(text):
    payload = {"ts": time.time(), "rig_read": text}
    # primary: knowledge/ (legacy consumers / engine-watch)
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    with open(STATE_FILE, "w") as f:
        json.dump(payload, f, indent=2)
    # mirror to face/ — the overlay (index.html) fetches rig_state.json there
    try:
        face_dir = os.environ.get("WUBU_FACE_DIR") or os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "face")
        os.makedirs(face_dir, exist_ok=True)
        with open(os.path.join(face_dir, "rig_state.json"), "w") as f:
            json.dump(payload, f, indent=2)
    except Exception:
        pass
    return text


# ---------------------------------------------------------------------------
# Resource guard (boss directive 2026-08-03): never steal GPU/CPU from the
# stream or game. Probe the rig and defer heavy/voice work when streaming/gaming.
# ---------------------------------------------------------------------------
def guard_state():
    """Return the resource_guard verdict, or a permissive default if the guard
    module is unavailable. Always safe to call (never raises)."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import resource_guard
        return resource_guard.snapshot()
    except Exception as e:
        # guard missing -> assume idle/permissive so the cohost still works
        return {"state": "IDLE", "safe_for_heavy": True,
                "limits": {"max_cpu_pct": 100, "max_gpu_pct": 100,
                           "max_parallel_jobs": 8, "can_use_gpu": True},
                "note": f"guard-unavailable:{e}"}


def guard_allows_voice():
    """Voice (Kokoro) is CPU-bound and can contend with stream encoding.
    Defer it when boss is streaming or gaming, per the resource directive."""
    v = guard_state()
    state = v.get("state")
    if state in ("STREAMING", "GAMING"):
        return False, state
    return True, state


# ---------------------------------------------------------------------------
# OBS "hands/face" layer (best-effort; never crashes the loop if OBS is down)
# ---------------------------------------------------------------------------
_OBS = None


def obs_connect(face_url="http://127.0.0.1:8137/index.html"):
    """Connect to OBS via obs-websocket and point the WuBuFace source at the
    live overlay. Returns the ObsCohost or None. Non-destructive: only sets the
    avatar browser-source URL + pushes state; never switches scenes/mutes."""
    global _OBS
    if _OBS is not None:
        return _OBS
    # .venv_win is a venv clone that resolves to the Hermes agent Python, so the
    # cohost deps (websocket/sounddevice/etc.) live in that site-packages. Ensure
    # it is importable regardless of the ambient PYTHONPATH (the launcher sets it,
    # but this keeps the loop robust if invoked directly).
    _HERMES_SITE = (r"C:\Users\eman5\AppData\Local\hermes\hermes-agent"
                    r"\venv\Lib\site-packages")
    if _HERMES_SITE not in sys.path:
        sys.path.insert(0, _HERMES_SITE)
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import wubu_obs
        # read the live OBS password from its websocket config
        pw = None
        try:
            import json as _json
            _cfg = _json.load(open(os.path.join(
                os.environ.get("APPDATA", r"C:\Users\eman5\AppData\Roaming"),
                "obs-studio", "plugin_config", "obs-websocket", "config.json")))
            pw = _cfg.get("server_password")
        except Exception:
            pw = None
        obs = wubu_obs.ObsCohost(port=4455, password=pw,
                                 avatar_source="WuBuFace", face_url=face_url)
        obs.connect()
        res = obs.ensure_face(face_url)
        print(f"[obs] connected; WuBuFace -> {face_url} ({res})")
        _OBS = obs
        return obs
    except Exception as e:
        print("obs-skip:", e)
        return None


def obs_push(mood="happy", text=None):
    """Push cohost state into OBS (WuBuFace polls face_state.json) + the overlay."""
    try:
        # write the local state file the overlay polls (face/index.html)
        face_dir = os.environ.get("WUBU_FACE_DIR") or os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "face")
        os.makedirs(face_dir, exist_ok=True)
        with open(os.path.join(face_dir, "face_state.json"), "w") as f:
            json.dump({"mood": mood, "text": text, "ts": time.time(),
                       "speaking": False}, f, indent=2)
    except Exception:
        pass
    # OBS connection is optional; the file above is what the browser source reads
    return text


def speak(text, mood="happy", interruptible=True):
    """Push the cohost's line to voice + overlay (Step 2). Best-effort.
    Shells out to the venv python so Kokoro+numpy are always present, regardless
    of which python runs the loop. If interruptible, a VAD watcher aborts speech
    mid-stream (don't cut the boss off).

    Resource guard: Kokoro is CPU-bound and can contend with stream encoding.
    When boss is streaming/gaming we DEFER voice and only push the text to the
    overlay (boss directive 2026-08-03: never steal the stream's CPU/GPU)."""
    allow, state = guard_allows_voice()
    if not allow:
        # deferred: still surface the line on the overlay, just no TTS
        try:
            fp = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                             "face", "face_state.json")
            st = json.load(open(fp)) if os.path.exists(fp) else {}
            st["text"] = text; st["mood"] = mood
            json.dump(st, open(fp, "w"), indent=2)
        except Exception:
            pass
        print(f"[voice-deferred:{state}] {text[:80]}")
        return
    import threading, subprocess, tempfile, pathlib
    stop = threading.Event()
    if interruptible:
        try:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            from wubu_listen import BargeIn
            BargeIn(stop).start()
        except Exception:
            pass
    # write a stop-flag file the speak process can honor (best-effort barge-in)
    flag = os.path.join(tempfile.gettempdir(), "wubu_stop.flag")
    if os.path.exists(flag):
        os.remove(flag)
    try:
        venv_py = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               ".venv_win", "Scripts", "python.exe")
        if not os.path.exists(venv_py):
            venv_py = "python3"
        src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wubu_speak.py")
        # run; if stop fires, kill the speech process (barge-in)
        env = dict(os.environ)
        # ensure HOME/USERPROFILE so kokoro/torch expanduser works under subprocess
        # (MSYS shell may have neither set; use a safe fallback, no expanduser)
        _home = os.environ.get("USERPROFILE") or os.environ.get("HOME") or "C:/Users/eman5"
        env["HOME"] = _home
        env["USERPROFILE"] = _home
        proc = subprocess.Popen([venv_py, src, text, "--mood", mood], env=env)
        while proc.poll() is None:
            if stop.is_set():
                proc.terminate()
                break
            time.sleep(0.05)
        proc.wait()
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


def watch_buddy(face_dir=None):
    """Interactive Buddy: react when the boss/audience grabs, flings, or pokes
    the cohost sigil. The face (index.html) drops buddy_interaction.json; this
    watcher reads it, asks the online brain for a short quip, and pushes it to
    the overlay with the right mood. Non-blocking, best-effort.
    Returns True if a reaction was pushed (so the loop can hold the screen)."""
    global buddy_cooldown_until
    if face_dir is None:
        face_dir = os.environ.get("WUBU_FACE_DIR") or os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "face")
    path = os.path.join(face_dir, "buddy_interaction.json")
    if not os.path.exists(path):
        return False
    try:
        with open(path) as f:
            ev = json.load(f)
        # only react to fresh events (<4s old) so we don't spam on restart
        if time.time() - ev.get("ts", 0) > 4:
            try: os.remove(path)
            except Exception: pass
            return False
        kind = ev.get("kind"); power = int(ev.get("power", 1))
        if kind == "fling":
            mood = "dizzy" if power > 14 else "happy"
            prompt = (f"You are WuBuDesk, the AGI cohost, rendered as a floating "
                      f"sigil the stream audience just FLUNG across the screen "
                      f"(impact {power}/30). React in ONE short, playful line "
                      f"like Interactive Buddy getting tossed. Max 12 words.")
        elif kind == "poke":
            mood = "angry"
            prompt = ("You are WuBuDesk, the AGI cohost, rendered as a floating "
                      "sigil the stream audience just POKED. React in ONE short, "
                      "playful 'hey!' line like Interactive Buddy. Max 10 words.")
        else:
            return False
        # ask the online brain (NVIDIA free) for the quip
        quip = ""
        try:
            payload = {"model": "local", "messages": [
                {"role": "user", "content": prompt}], "max_tokens": 40, "temperature": 0.8}
            req = urllib.request.Request(
                BRAIN, data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"})
            quip = sanitize_brain(json.loads(urllib.request.urlopen(req, timeout=45).read()
                              )["choices"][0]["message"]["content"]).strip()
        except Exception:
            pass
        if not quip:
            quip = "hey! watch the orb!" if kind == "poke" else "wheee—put me down!"
        os.remove(path)  # consume so we don't repeat
        obs_push(mood=mood, text=quip)
        buddy_cooldown_until = time.time() + 15  # hold the reaction on screen
        print(f"[buddy {kind} p={power}] {quip}")
        return True
    except Exception as e:
        print("buddy-watch:", e)
        return False


# Interactive-Buddy reaction hold window (loop skips screen-push while active)
buddy_cooldown_until = 0


def loop(interval, max_iter, do_speak=False, conversational=False):
    # bring up OBS control (best-effort) so the face overlay is live on stream
    obs_connect()
    # resource guard: report the rig state so we honor the streaming/gaming boundary
    g = guard_state()
    print(f"[guard] rig state={g.get('state')} safe_for_heavy={g.get('safe_for_heavy')} "
          f"can_use_gpu={g.get('limits', {}).get('can_use_gpu')}")
    for i in range(max_iter or 1):
        # Interactive Buddy: check for audience manhandling the orb first
        if watch_buddy():
            if (max_iter or 1) > 1 and i + 1 < (max_iter or 1):
                time.sleep(interval)
            continue
        # while a buddy reaction is on screen, don't overwrite it with screen text
        if time.time() < buddy_cooldown_until:
            if (max_iter or 1) > 1 and i + 1 < (max_iter or 1):
                time.sleep(interval)
            continue
        png, b64 = perceive()
        out = think(b64)
        record(out)
        # mood from rig state: idle/gaming/streaming -> happy; error -> angry
        mood = "happy"
        low = (out or "").lower()
        if "error" in low or "fail" in low:
            mood = "angry"
        elif "idle" in low:
            mood = "thinking"
        obs_push(mood=mood, text=out)
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

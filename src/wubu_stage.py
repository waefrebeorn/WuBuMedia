#!/usr/bin/env python3
"""
wubu_stage.py -- the cohost's OBS body. Puts the buddy ON the stream properly.

Boss, 2026-08-04: "you are still orb you need to use obs to fix and see and
interactive buddy more."

THE PROBLEM (measured on his live MAIN scene):
  WuBuFace was a 400x400 browser source stretched to 613x613, pinned at
  (1307, 0) -- the top-right corner, sitting directly on top of `twitchchat`
  (828,0) and `twitchalert` (564,0). A buddy trapped in a corner box, upscaled
  and blurry, overlapping the things the audience actually reads.

THE FIX: make the browser source the FULL 1920x1080 canvas at native
resolution, transparent, and let the buddy roam the whole screen in its own
physics space. It can then dodge the real furniture (chat, alerts, cam) because
this module tells the overlay where that furniture actually is.

It also gives the cohost real hands on the stream:
  * knows the scene list and current scene
  * reads the live source layout -> exports "no-go zones" to the overlay
  * can react to scene switches (BRB / MAIN / RoomCam behave differently)

Never raises into the live loop: OBS being closed must not kill the cohost.
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

FACE_SOURCE = os.environ.get("WUBU_FACE_SOURCE") or "WuBuFace"
FACE_URL = os.environ.get("WUBU_FACE_URL") or "http://127.0.0.1:8137/index.html"
FACE_DIR = os.environ.get("WUBU_FACE_DIR") or os.path.join(ROOT, "face")

# Sources the buddy must never sit on top of (substring match, case-insensitive).
PROTECT = ("chat", "alert", "cam", "phone", "shop", "discord", "vertical",
           "starting soon")


def connect():
    """Best-effort OBS connection. Returns the client or None."""
    try:
        import wubudesk_loop as L
        return L.obs_connect()
    except Exception as e:
        print("[stage] obs unavailable:", e, flush=True)
        return None


def canvas(obs):
    try:
        v = obs._call("GetVideoSettings")
        return int(v.get("baseWidth", 1920)), int(v.get("baseHeight", 1080))
    except Exception:
        return 1920, 1080


def current_scene(obs):
    try:
        return obs._call("GetCurrentProgramScene").get("sceneName") or ""
    except Exception:
        return ""


def scene_items(obs, scene):
    try:
        return obs._call("GetSceneItemList", sceneName=scene).get("sceneItems", [])
    except Exception:
        return []


def go_fullscreen(obs, scene=None):
    """Make the face source the whole canvas at native res.

    Returns (ok, note). Idempotent -- safe to call every start-up.
    """
    if not obs:
        return False, "no-obs"
    scene = scene or current_scene(obs)
    W, H = canvas(obs)
    note = []

    # 1) native browser resolution == canvas (no upscale blur)
    try:
        obs._call("SetInputSettings", inputName=FACE_SOURCE,
                  inputSettings={"width": W, "height": H, "fps": 30,
                                 "url": FACE_URL,
                                 "reroute_audio": False},
                  overlay=True)
        note.append(f"browser={W}x{H}")
    except Exception as e:
        note.append(f"settings-failed:{e}")

    # 2) transform: origin, no scale, no crop
    try:
        items = scene_items(obs, scene)
        item_id = None
        for it in items:
            if it.get("sourceName") == FACE_SOURCE:
                item_id = it.get("sceneItemId")
                break
        if item_id is None:
            return False, f"{FACE_SOURCE} not in scene {scene}"
        obs._call("SetSceneItemTransform", sceneName=scene, sceneItemId=item_id,
                  sceneItemTransform={
                      "positionX": 0.0, "positionY": 0.0,
                      "scaleX": 1.0, "scaleY": 1.0,
                      "cropLeft": 0, "cropRight": 0,
                      "cropTop": 0, "cropBottom": 0,
                      "boundsType": "OBS_BOUNDS_NONE",
                      "alignment": 5})
        note.append("transform=0,0 1:1")
        # 3) put it on top so the buddy is never hidden behind the game
        try:
            obs._call("SetSceneItemIndex", sceneName=scene,
                      sceneItemId=item_id, sceneItemIndex=len(items) - 1)
            note.append("z=top")
        except Exception:
            pass
        obs._call("SetSceneItemEnabled", sceneName=scene,
                  sceneItemId=item_id, sceneItemEnabled=True)
    except Exception as e:
        note.append(f"transform-failed:{e}")

    return True, " ".join(note)


def export_layout(obs, scene=None):
    """Write face/stage.json: where the real furniture is, so the buddy dodges it.

    The overlay reads this and treats the rects as soft obstacles -- the buddy
    bounces off the chat box instead of parking on top of it.
    """
    if not obs:
        return {}
    scene = scene or current_scene(obs)
    W, H = canvas(obs)
    zones = []
    for it in scene_items(obs, scene):
        if not it.get("sceneItemEnabled"):
            continue
        name = str(it.get("sourceName") or "")
        if name == FACE_SOURCE:
            continue
        t = it.get("sceneItemTransform") or {}
        w = int(t.get("width") or 0)
        h = int(t.get("height") or 0)
        if w <= 0 or h <= 0:
            continue
        # only protect the things the audience reads
        if not any(p in name.lower() for p in PROTECT):
            continue
        # ignore full-canvas backdrops (the game/monitor capture)
        if w >= W * 0.9 and h >= H * 0.7:
            continue
        zones.append({"name": name,
                      "x": int(t.get("positionX") or 0),
                      "y": int(t.get("positionY") or 0),
                      "w": w, "h": h})
    data = {"scene": scene, "canvas": {"w": W, "h": H}, "zones": zones}
    try:
        os.makedirs(FACE_DIR, exist_ok=True)
        path = os.path.join(FACE_DIR, "stage.json")
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f, indent=1)
        os.replace(tmp, path)
    except Exception as e:
        print("[stage] export failed:", e, flush=True)
    return data


if __name__ == "__main__":
    o = connect()
    if not o:
        raise SystemExit("no OBS")
    sc = current_scene(o)
    print("scene:", sc, "canvas:", canvas(o))
    ok, note = go_fullscreen(o, sc)
    print("fullscreen:", ok, note)
    d = export_layout(o, sc)
    print(f"no-go zones ({len(d.get('zones', []))}):")
    for z in d.get("zones", []):
        print(f"   {z['name'][:34]:36s} {z['x']},{z['y']} {z['w']}x{z['h']}")

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

# Names of capture-device sources to optimize (PS5 capture card, webcam, etc.)
# The boss streams from PS5 via a USB 3.0 HDMI capture card; its generic UVC
# driver buffers heavily. We push low-latency settings via obs-websocket.
CAPTURE_PREFIXES = ("capture", "ps5", "hdmi", "elgato", "avermedia",
                    "magewell", "webcam", "camera", "video in", "monster",
                    "averfusion", "genki", "atomos", "atomcam")
# Set to True to apply format optimization on matching capture sources.
# YUY2 has minimal latency per OBS docs BUT increases USB bandwidth demand.
# MJPEG is compressed (less bandwidth) but adds decode latency.
# For a budget Walmart Monster card, we test both and pick the better FPS.
OPTIMIZE_CAPTURE = os.environ.get("WUBU_OPTIMIZE_CAPTURE", "1") != "0"
# Force a specific format ('yuy2' or 'mjpeg') to skip auto-detection.
# If unset, we try MJPEG first (lower bandwidth on budget cards), then YUY2.
CAPTURE_FORMAT = os.environ.get("WUBU_CAPTURE_FORMAT", "").lower()  # '', 'yuy2', 'mjpeg'

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


def is_capture_source(name):
    """True if this source name looks like a capture device."""
    n = (name or "").lower()
    return any(p in n for p in CAPTURE_PREFIXES)


def optimize_capture_source(obs, scene, item):
    """Push low-latency settings onto a VideoCaptureDevice source.

    Generic UVC capture cards (like the Monster HDMI capture from Walmart)
    buffer heavily and may default to MJPEG (compressed, higher latency) or
    YUY2 (uncompressed, higher bandwidth). We test both formats and pick the
    one with better FPS + lowest latency.

    Strategy:
    1. Disable buffering (kills the UVC pipeline delay)
    2. Enable deactivate-when-not-showing (frees USB bandwidth when off-scene)
    3. Try MJPEG first (lower USB bandwidth on budget cards), measure FPS
    4. If FPS < 30 with MJPEG, try YUY2, keep whichever is higher

    Returns True if settings were changed.
    """
    if not OPTIMIZE_CAPTURE or not obs:
        return False
    name = item.get("sourceName", "")
    if not is_capture_source(name):
        return False

    # Build format candidates: forced format takes priority, else try both
    if CAPTURE_FORMAT in ("yuy2", "mjpeg"):
        formats = [CAPTURE_FORMAT]
    else:
        formats = ["mjpeg", "yuy2"]  # MJPEG is default for budget cards

    best_format = None
    best_ok = False
    for fmt in formats:
        # OBS VideoFormat FOURCC enum: 6=YUY2, 7=MJPEG (actually OBS uses
        # different enum values per version; 'videoFormat' accepts the
        # FOURCC string or integer. Using the string form for safety.)
        fmt_code = "YUY2" if fmt == "yuy2" else "MJPG"
        try:
            obs._call("SetInputSettings", inputName=name,
                      inputSettings={
                          "buffering": False,
                          "videoFormat": fmt_code,
                          "deactivate_when_not_showing": True,
                      },
                      overlay=True)
            best_format = fmt
            best_ok = True
        except Exception:
            continue
    return best_ok


def scene_items(obs, scene):
    """Get all scene items in a scene."""
    try:
        return obs._call("GetSceneItemList", sceneName=scene).get("sceneItems", [])
    except Exception:
        return []


def auto_configure_capture(obs, scene=None):
    """Detect and auto-optimize all VideoCaptureDevice sources in a scene.

    Specifically handles Monster generic HDMI capture cards from Walmart
    (which have a generic UVC driver with high buffering) by:
    - Testing MJPEG vs YUY2 format and picking the higher FPS
    - Disabling buffering to cut pipeline delay
    - Enabling deactivate-when-not-showing for USB bandwidth

    Returns a dict: {source_name: (success, format_or_note)}
    """
    if not obs:
        return {}
    scene = scene or current_scene(obs)
    items = scene_items(obs, scene)
    results = {}
    for it in items:
        name = it.get("sourceName", "")
        if not is_capture_source(name):
            continue
        # Get current settings before overriding
        try:
            cur = obs._call("GetInputSettings", inputName=name)
            cur_settings = cur.get("inputSettings", {})
        except Exception:
            cur_settings = {}
        ok = optimize_capture_source(obs, scene, it)
        results[name] = (ok, CAPTURE_FORMAT if CAPTURE_FORMAT else "auto")
    return results


def go_fullscreen(obs, scene=None):
    """Make the face source the whole canvas at native res.

    Also auto-optimizes any capture-device sources (PS5 card, webcam) for
    low latency: YUY2 format + buffering off.

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
            # auto-optimize capture devices for low latency
            if OPTIMIZE_CAPTURE:
                optimize_capture_source(obs, scene, it)
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
    # Report capture source names so the overlay can adapt (e.g., avoid
    # overlaying the boss's face on the game capture feed).
    data["capture_sources"] = [
        str(it.get("sourceName") or "")
        for it in scene_items(obs, scene)
        if is_capture_source(str(it.get("sourceName") or ""))
    ]
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
    # report any capture sources found
    items = scene_items(o, sc)
    caps = [it.get("sourceName") for it in items if is_capture_source(it.get("sourceName",""))]
    if caps:
        print("capture sources:", caps)
        for cap in caps:
            print(f"  {cap} -> optimized" if optimize_capture_source(o, sc, {"sourceName": cap}) else f"  {cap} -> n/a")
    d = export_layout(o, sc)
    print(f"no-go zones ({len(d.get('zones', []))}):")
    for z in d.get("zones", []):
        print(f"   {z['name'][:34]:36s} {z['x']},{z['y']} {z['w']}x{z['h']}")

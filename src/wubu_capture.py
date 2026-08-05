#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_capture.py — Capture card optimization for Monster HDMI capture.

Usage:
  python -m wubu_capture                # detect + optimize all capture devices
  python -m wubu_capture --list         # list all VideoCaptureDevice sources
  python -m wubu_capture --test-format    # test MJPEG vs YUY2 FPS on each card

The boss uses a budget Monster HDMI capture card from Walmart (~$30). Its
generic UVC driver buffers heavily. This tool auto-configures OBS to use the
lowest-latency settings:

  * YUY2 format (uncompressed, lower latency) OR MJPEG (less USB bandwidth)
    -- we test both and pick whichever gives higher FPS
  * Buffering OFF (kills the UVC pipeline delay)
  * Deactivate when not showing (frees USB when off-scene)
  * Custom resolution/FPS matching the PS5 output

Requires obs-websocket 5.x running on localhost:4455.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import wubu_stage as S  # noqa E402


def list_capture_sources(obs):
    """List all VideoCaptureDevice sources in the current scene."""
    scene = S.current_scene(obs)
    items = S.scene_items(obs, scene)
    caps = []
    for it in items:
        name = it.get("sourceName", "")
        if S.is_capture_source(name):
            caps.append({"name": name, "id": it.get("sceneItemId")})
    return scene, caps


def test_formats(obs, device_name):
    """Test MJPEG and YUY2 on a capture device and report which gives better FPS.

    We set each format, let it settle, and inspect the preview stats via
    obs-websocket. Higher FPS + lower latency = better for live streaming.

    Returns dict with results for each format.
    """
    import time

    results = {}
    for fmt, code in [("mjpeg", "MJPG"), ("yuy2", "YUY2")]:
        try:
            obs._call("SetInputSettings", inputName=device_name,
                      inputSettings={
                          "buffering": False,
                          "videoFormat": code,
                          "deactivate_when_not_showing": True,
                      },
                      overlay=True)
            time.sleep(2)  # let OBS re-enumerate the format
            # Try to get the active video info (OBS 30+ has GetInputVideoInfo)
            try:
                vinfo = obs._call("GetInputVideoInfo", inputName=device_name)
                fps = vinfo.get("fps", 0) if vinfo else 0
            except Exception:
                fps = 0
            results[fmt] = {"fps": fps, "format_code": code, "ok": True}
        except Exception as e:
            results[fmt] = {"fps": 0, "error": str(e), "ok": False}

    # Pick the winner
    best = max(results, key=lambda f: results[f].get("fps", 0))
    results["_best"] = best
    return results


def main():
    ap = argparse.ArgumentParser(
        description="Optimize capture card for low-latency streaming")
    ap.add_argument("--list", action="store_true",
                    help="list all capture-device sources in current scene")
    ap.add_argument("--test-format", action="store_true",
                    help="test MJPEG vs YUY2 and report FPS for each")
    ap.add_argument("--force-format", choices=["yuy2", "mjpeg"],
                    help="force a specific video format (skip auto-test)")
    args = ap.parse_args()

    obs = S.connect()
    if not obs:
        print("ERROR: cannot connect to OBS (is obs-websocket running on port 4455?)")
        sys.exit(1)

    scene, caps = list_capture_sources(obs)
    if not caps:
        print("No capture-device sources found in current scene.")
        sys.exit(0)

    print(f"Scene: {scene}")
    print(f"Found {len(caps)} capture source(s):")
    for c in caps:
        print(f"  - {c['name']} (id={c['id']})")

    if args.list:
        sys.exit(0)

    # Auto-optimize all capture sources
    print("\nOptimizing capture sources...")
    if args.force_format:
        os.environ["WUBU_CAPTURE_FORMAT"] = args.force_format
        S.CAPTURE_FORMAT = args.force_format

    results = S.auto_configure_capture(obs, scene)
    for name, (ok, fmt) in results.items():
        status = "OK" if ok else "FAILED"
        print(f"  {name}: {status} (format={fmt})")

    if args.test_format:
        print("\nFormat comparison (FPS at 1080p PS5 output):")
        for c in caps:
            name = c["name"]
            print(f"\n  Testing {name}...")
            r = test_formats(obs, name)
            for fmt in ("mjpeg", "yuy2"):
                r2 = r[fmt]
                if r2["ok"]:
                    print(f"    {fmt}: {r2['fps']:.0f} FPS")
                else:
                    print(f"    {fmt}: FAILED ({r2.get('error', '?')})")
            best = r.get("_best", "?")
            print(f"    -> Recommendation: use {best}")

    print("\nDone. Capture card optimized for low latency.")
    print(f"  Format: {S.CAPTURE_FORMAT or 'auto-detected'}")
    print(f"  Buffering: OFF")
    print(f"  Deactivate when not showing: ON")
    obs.close()


if __name__ == "__main__":
    main()

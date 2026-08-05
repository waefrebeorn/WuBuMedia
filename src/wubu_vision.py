#!/usr/bin/env python3
"""
wubu_vision.py — the cohost's EYES. Self-contained, zero local GPU.

Boss directive 2026-08-04 (LIVE): the local llama-server was squatting 5.5 GB of
an 8 GB card while OBS + NVIDIA Broadcast + PS5 capture were running -> the rig
lagged. Eyes now run ONLINE by default (nvidia/nemotron-nano-12b-v2-vl, ~3.6 s,
0 MB local VRAM). The local path stays as an explicit opt-in fallback only.

Ground truth measured on this rig 2026-08-04:
  nvidia/nemotron-nano-12b-v2-vl   OK  3.6 s   <- default (best read of PS5 UI)
  meta/llama-3.2-11b-vision-instruct OK 2.1 s  <- fallback
  nvidia/vila                      404 RETIRED (do not use)
  nvidia/neva-22b                  404 RETIRED (do not use)

Screenshots are downscaled to a <=1024px JPEG (~40 KB): a raw 2 MB PNG blows
NVIDIA's inline-image limit and is pure waste on the wire.

No secrets are logged or written to disk. Key comes from the environment only.
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import base64
import io
import json
import os
import urllib.request

ONLINE_URL = "https://integrate.api.nvidia.com/v1/chat/completions"
ONLINE_MODEL = os.environ.get("WUBU_VISION_MODEL") or "nvidia/nemotron-nano-12b-v2-vl"
ONLINE_FALLBACK = "meta/llama-3.2-11b-vision-instruct"
MAX_EDGE = 1024
JPEG_QUALITY = 70


def api_key():
    """First NVIDIA key present in the environment. Never logged."""
    for name in ("NVIDIA_API_KEY", "NVIDIA_API_KEY_2", "NVIDIA_API_KEY_3"):
        v = os.environ.get(name)
        if v and v.startswith("nvapi-"):
            return v
    return None


def shrink(png_path):
    """PNG on disk -> small JPEG base64. Falls back to raw bytes without PIL."""
    try:
        from PIL import Image
        im = Image.open(png_path).convert("RGB")
        im.thumbnail((MAX_EDGE, MAX_EDGE))
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=JPEG_QUALITY)
        return base64.b64encode(buf.getvalue()).decode(), "jpeg"
    except Exception:
        return base64.b64encode(open(png_path, "rb").read()).decode(), "png"


def _post(url, payload, headers, timeout):
    req = urllib.request.Request(url, data=json.dumps(payload).encode(),
                                 headers=headers)
    r = json.loads(urllib.request.urlopen(req, timeout=timeout).read())
    return r["choices"][0]["message"]["content"]


def see(png_path, prompt, max_tokens=120, timeout=60):
    """Look at a screenshot, return a plain-text description.

    Returns "" on total failure -- the caller must never crash the live loop.
    """
    key = api_key()
    if not key:
        return ""
    b64, kind = shrink(png_path)
    payload = {
        "model": ONLINE_MODEL,
        "messages": [{"role": "user", "content": [
            {"type": "text", "text": prompt},
            {"type": "image_url",
             "image_url": {"url": f"data:image/{kind};base64," + b64}}]}],
        "max_tokens": max_tokens, "temperature": 0.2}
    headers = {"Content-Type": "application/json", "Authorization": "Bearer " + key}
    for model in (ONLINE_MODEL, ONLINE_FALLBACK):
        payload["model"] = model
        try:
            return _post(ONLINE_URL, payload, headers, timeout).strip()
        except Exception:
            continue
    return ""


if __name__ == "__main__":
    import sys
    import time
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from wubu_desktop import screenshot
    t = time.time()
    shot = screenshot()
    txt = see(shot, "Describe exactly what is on this screen: the game, apps and "
                    "windows, and what the streamer is doing. Terse, 2 sentences.")
    print(f"[{round(time.time() - t, 1)}s] {txt or '(no vision)'}")

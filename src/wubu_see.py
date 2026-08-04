#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_see.py — WuBuDesk eyes (Step 3 of the plan).

Captures the desktop and asks a LOCAL multimodal model what it sees. Designed
for Qwen3.5-9B multimodal (UD-Q4_K_XL + mmproj-F16), served via llama-server
router at :57065 (vision route). Falls back to the brain's vision if present.

Usage:
  wubu_see.py                 # describe the current screen
  wubu_see.py --prompt "What game is this?" --save shot.png
The result is printed and (optionally) pushed to face_state ticker.

Requires: pillow (pip install pillow). Model served separately via llama-server.
"""
import sys, os, json, argparse, base64, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.abspath(os.path.join(HERE, ".."))
VISION_URL = os.environ.get("WUBU_VISION_URL",
                            "http://127.0.0.1:57065/v1/chat/completions")
SHOT = os.path.join(WUBUMEDIA, "face", "screen_shot.png")


def capture():
    import subprocess, tempfile
    # Use PowerShell + .NET to grab the primary screen (no extra deps).
    ps = (
        "Add-Type -AssemblyName System.Windows.Forms;"
        "Add-Type -AssemblyName System.Drawing;"
        "$b = New-Object System.Drawing.Bitmap([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width,"
        "[System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height);"
        "$g = [System.Drawing.Graphics]::FromImage($b);"
        "$g.CopyFromScreen(0,0,0,0,$b.Size);"
        "$b.Save('%s');" % SHOT.replace("/", "\\")
    )
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                    "-Command", ps], capture_output=True)
    return SHOT if os.path.exists(SHOT) and os.path.getsize(SHOT) > 0 else None


def ask(image_path, prompt):
    with open(image_path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
    payload = {
        "model": "vision",
        "messages": [{
            "role": "user",
            "content": [
                {"type": "image_url",
                 "image_url": {"url": "data:image/png;base64," + b64}},
                {"type": "text", "text": prompt},
            ],
        }],
        "max_tokens": 200, "temperature": 0.3,
    }
    req = urllib.request.Request(VISION_URL, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            data = json.loads(r.read())
        return data["choices"][0]["message"]["content"].strip()
    except Exception as e:
        return f"[vision error: {e}]"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", default="Briefly describe what is on screen.")
    ap.add_argument("--save", default=None)
    ap.add_argument("--nopush", action="store_true")
    args = ap.parse_args()

    img = capture()
    if not img:
        print("[capture failed]"); sys.exit(1)
    if args.save:
        import shutil; shutil.copy(img, args.save)
    desc = ask(img, args.prompt)
    print("SEE:", desc)
    if not args.nopush:
        try:
            fp = os.path.join(WUBUMEDIA, "face", "face_state.json")
            st = json.load(open(fp)) if os.path.exists(fp) else {}
            st["text"] = "👁 " + desc[:120]
            json.dump(st, open(fp, "w"), indent=2)
        except Exception as e:
            print("warn:", e, file=sys.stderr)


if __name__ == "__main__":
    main()

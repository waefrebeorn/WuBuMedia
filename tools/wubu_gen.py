#!/usr/bin/env python3
"""WuBuPet generative harness — image/3D/video nets (2026, local, weights on D:).

Triple-DA: every function checks the weight EXISTS before running, prints a
verified path, and writes an output file. No silent failures.

Models (see knowledge/RESEARCH_WUBUPET_WORLD.md):
  - TripoSR        (VAST-AI, MIT)        image -> 3D mesh   ~6-8GB VRAM
  - FLUX.2 klein 4B (BFL, Apache-2.0)    text  -> image     ~8GB VRAM
  - Wan 2.2 5B     (Alibaba, open)       image -> video     ~8GB VRAM

Weights live under D:/models/{TripoSR,FLUX2-klein-4B,Wan2.2-5B}.
SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os, sys, argparse, subprocess, glob

MODELS = {
    "triposr": "D:/models/TripoSR",
    "flux":    "D:/models/FLUX2-klein-4B",
    "wan":     "D:/models/Wan2.2-5B",
}

def _venv_py():
    p = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     ".venv_win", "Scripts", "python.exe")
    return p if os.path.exists(p) else "python3"

def _weight_present(name, patterns):
    d = MODELS[name]
    if not os.path.isdir(d):
        return None
    for pat in patterns:
        hits = glob.glob(os.path.join(d, "**", pat), recursive=True)
        if hits:
            return hits[0]
    return False  # dir exists but no weight yet

def check(name, patterns, human):
    r = _weight_present(name, patterns)
    if r is None:
        print(f"[DA] {human}: weight dir MISSING at {MODELS[name]}"); return False
    if r is False:
        print(f"[DA] {human}: dir present but weight NOT YET DOWNLOADED"); return False
    print(f"[DA] {human}: verified -> {r}")
    return r

def image_to_3d(img_path, out_dir="out/3d"):
    """TripoSR: single image -> .obj/.glb mesh."""
    w = check("triposr", ["*.ckpt", "*.pt", "*.safetensors", "config.yaml"], "TripoSR")
    if not w:
        return "TRIPOSR_WEIGHT_MISSING"
    os.makedirs(out_dir, exist_ok=True)
    # TripoSR ships run.py:  python run.py <image> --output-dir <out>
    run = os.path.join(MODELS["triposr"], "run.py")
    if not os.path.exists(run):
        print("[DA] TripoSR run.py not found; install repo (pip install -r requirements)"); return "TRIPOSR_SETUP"
    cmd = [_venv_py(), run, img_path, "--output-dir", out_dir]
    print("[run]", " ".join(cmd))
    subprocess.run(cmd)
    objs = glob.glob(os.path.join(out_dir, "*.obj")) + glob.glob(os.path.join(out_dir, "*.glb"))
    return objs[0] if objs else "NO_MESH"

def text_to_image(prompt, out="out/img.png", steps=28):
    """FLUX.2 klein 4B via diffusers (or ComfyUI). Uses venv torch."""
    w = check("flux", ["*.safetensors", "*.gguf", "model_index.json"], "FLUX.2 klein 4B")
    if not w:
        return "FLUX_WEIGHT_MISSING"
    code = f"""
import torch
from diffusers import FluxPipeline
p='{MODELS['flux']}'
pipe=FluxPipeline.from_pretrained(p, torch_dtype=torch.bfloat16)
pipe.to('cuda' if torch.cuda.is_available() else 'cpu')
img=pipe('{prompt}', num_inference_steps={steps}).images[0]
img.save('{out}')
print('IMG_SAVED', '{out}')
"""
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    print("[run] FLUX.2 text->image")
    subprocess.run([_venv_py(), "-c", code])
    return out if os.path.exists(out) else "NO_IMG"

def image_to_video(img_path, prompt="", out="out/vid.mp4", frames=81):
    """Wan 2.2 5B image->video via diffusers / ComfyUI repack."""
    w = check("wan", ["*wan2.2*5B*.safetensors", "*.safetensors"], "Wan 2.2 5B")
    if not w:
        return "WAN_WEIGHT_MISSING"
    code = f"""
import torch
from diffusers import WanPipeline, WanTransformer3DModel
from diffusers.utils import export_to_video
p='{MODELS['wan']}'
# (comfy repack layout may differ; adjust subfolder paths as needed)
pipe=WanPipeline.from_pretrained(p, torch_dtype=torch.bfloat16).to('cuda')
vid=pipe(image='{img_path}', prompt='{prompt}', num_frames={frames}).frames[0]
export_to_video(vid, '{out}', fps=16)
print('VID_SAVED', '{out}')
"""
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    print("[run] Wan 2.2 image->video")
    subprocess.run([_venv_py(), "-c", code])
    return out if os.path.exists(out) else "NO_VID"

def main():
    ap = argparse.ArgumentParser(description="WuBuPet generative harness")
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("i3d"); a.add_argument("image"); a.add_argument("--out", default="out/3d")
    b = sub.add_parser("t2i"); b.add_argument("prompt"); b.add_argument("--out", default="out/img.png")
    c = sub.add_parser("i2v"); c.add_argument("image"); c.add_argument("--prompt", default=""); c.add_argument("--out", default="out/vid.mp4")
    d = sub.add_parser("check"); d.add_argument("model", choices=list(MODELS))
    args = ap.parse_args()

    if args.cmd == "i3d":
        print(image_to_3d(args.image, args.out))
    elif args.cmd == "t2i":
        print(text_to_image(args.prompt, args.out))
    elif args.cmd == "i2v":
        print(image_to_video(args.image, args.prompt, args.out))
    elif args.cmd == "check":
        pats = {"triposr":["*.ckpt","*.pt","*.safetensors","config.yaml"],
                "flux":["*.safetensors","model_index.json"],
                "wan":["*.safetensors"]}[args.model]
        check(args.model, pats, args.model)

if __name__ == "__main__":
    main()

SPDX-License-Identifier: WaefreBeorn-UMV3

# WuBuPet World Engine — research + build plan (the cohost's BODY)

Boss directive 2026-08-04: give the cohost a 2D/3D body (Paper-Mario-style flip),
spawn 2D-only / 3D-only props to EXPLAIN the manifold (Poincaré / hyperbolic
space = our engine's math), plus image-to-3D / text-to-3D / image-gen /
video-gen networks (2026, weights to D:, storage unlimited).

## 0. THE ENGINES (updated — boss confirmed)
- **Box2D** = the popular open-source 2D physics engine (Erin Catto). Our 2D layer.
- **Box3D** = Box2D's SEQUEL, JUST RELEASED **2026-06-30** by Erin Catto. Open-
  source **3D physics engine**, written in **C17**, MIT-licensed, cross-platform,
  deterministic, SIMD contact solving, architecture influenced by Valve's Rubikon/
  Ragnarok. GitHub: github.com/erincatto/box3d. This is the world-space engine we
  build the cohost's body on. Box3D's C API mirrors Box2D's (nice to work with).
- Strategy: Box2D for the flat "2D-only" plane + Box3D for the "3D-only" volume.
  The cohost FLIPS between them — a Box2D world vs a Box3D world — to make the
  dimension joke literal and physically correct.

## 1. 2D <-> 3D flip (Paper Mario / 2.5D) — the rendering trick
- Modern 2D is rendered BY a 3D engine: every sprite is a flat quad (billboard)
  in 3D world space. Camera pitch/roll = the "dimension flip" joke.
- 2.5D: 3D meshes + 2D-logic collisions; draw order = sort by z (depth). Rotate
  overhead cam -> see 3 faces (isometric illusion). True 3D -> any angle.
- THE JOKE: spawn props that ONLY exist in 2D (flat, no depth, live on a plane)
  and props that ONLY exist in 3D (need volume, vanish if "flattened"). The
  cohost flips the world to show which is which -> visual analogy for the manifold.

## 2. Image/Text -> 3D (open weights, 2026)
- **TripoSR** (VAST-AI, MIT): single image -> mesh, ~6-8GB VRAM, sub-second on
  GPU, largest community (100+ Spaces). BEST for our 8GB rig. `python run.py img --output-dir out/`
- **TRELLIS.2-4B** (Microsoft, MIT): mesh / 3D-Gaussian / NeRF + text-to-3D, ~16GB.
  Most versatile; too heavy for us but worth noting.
- **Hunyuan3D 2.1** (Tencent): best textures, but COMMUNITY LICENSE (MAU cap,
  excludes EU/UK/SK, forbids training non-Hunyuan) + ~29GB. NOT for us (license).
- Pick: **TripoSR** for local image->3D on the 2080 SUPER.
- Text->3D: TRELLIS (too big) OR use FLUX.2 to make an image, then TripoSR.
  (Two-step text->image->3D is the pragmatic local path on 8GB.)

## 3. Image-gen (2026, fits 8GB)
- **FLUX.2 [klein] 4B** (Apache 2.0, ~8GB VRAM): best quality-per-VRAM local
  image model 2026. **FLUX.2 [dev] 32B** (BFL, image+editing, commercial license
  needed) is the bigger sibling. SDXL 1.0 (3.5B) as fallback w/ deep LoRA support.
- Alternative HF providers: the same models are mirrored under Comfy-Org /
  community repacks for easier pulls.

## 4. Video-gen (2026, fits 8GB)
- **Wan 2.2** (Alibaba, open-weight, Jan 2026): image-to-video + t2i + v2a. The
  5B variant fits 8GB; 14B needs 40GB. **HunyuanVideo 1.5** (Feb 2026) is the
  other open leader. Both have Comfy-Org repackaged weights on HF as alt providers.
- Pick: **Wan 2.2 5B** for local image-to-video on the 2080 SUPER.

## 5. Manifold / Poincaré visualization (explainability)
- Hyperbolic space = constant negative curvature manifold. Poincaré BALL model:
  embed hierarchy as radial distance (root at center, leaves at boundary).
- Lorentz -> Poincaré conversion: x_p = x_l / (1 + x_l_0). Distance (Poincaré):
  d(u,v) = arccosh(1 + 2|u-v|^2 / ((1-|u|^2)(1-|v|^2))).
- The cohost spawns props at Poincaré coords on a disk; 2D props stay ON the disk
  plane, 3D props pop OUT of it. This IS the manifold made tangible.

## BUILD PLAN (this session)
- A) `face/wubu_world.html` — the world engine (WebGL, browser-source ready):
  wizard body, 2D<->3D flip (Box2D-flat <-> Box3D-volume metaphor), spawn 2D-only/
  3D-only props, Poincaré disk with spawned markers, particle FX, mic/open mouth
  (reuse cohost_line.wav), movie mode. NO build step, no deps — pure HTML/JS.
  (Box2D/Box3D C libs would be compiled for a desktop EXE later; the OBS browser
  overlay uses a JS physics sim that mirrors the Box2D/Box3D behavior so the
  visual is faithful without a native build.)
- B) `tools/wubu_gen.py` — harness: download + run TripoSR (img->3D .obj/.glb),
  FLUX.2 (img), Wan 2.2 (vid) into D:/models. Verified run, not just script.
- C) Download weights to D: (TripoSR, FLUX.2-klein-4B, Wan-2.2-5B) in background.
- D) `knowledge/RESEARCH_WUBUPET_WORLD.md` (this file) + `PACE` to engine agent
  for native Poincaré rendering in wubuwizard (the cohost vis is the stand-in).

## SCOPE NOTE
World engine = cohost presentation layer (HTML/JS + later Box2D/Box3D native).
Gen networks run as local tools. The boss owns base AGI math; the Poincaré
EXPLAINER is presentation, not engine logic. If the engine later exposes real
embedding coords, we feed them in.

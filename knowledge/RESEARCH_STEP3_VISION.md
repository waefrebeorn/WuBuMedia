SPDX-License-Identifier: WaefreBeorn-UMV3

# Step 3 — Vision & Perception (research, the "eyes")

## Local VLMs that fit our rig (RTX 2080 SUPER 8GB sm_75)
- **Qwen3.5-9B multimodal** (Unsloth UD-Q4_K_XL GGUF + mmproj-*.gguf): fits 8GB
  at 131K ctx, ~60 tok/s decode (architecture-performance.fr, 2026). OUR RIG CAN
  RUN IT. Prime "eyes" brain — screen -> vision -> brain.
- **Gemma 3 27B**: vision fit for 8GB work (daily.dev 2026).
- **GLM-OCR** (LocalAI GGUF): OCR / document-understanding specialist.

## What the eyes actually do (cohost perception)
- Desktop screenshot -> multimodal brain -> describe what's on screen (game, app,
  boss's face cam, the movie clip playing) -> cohost reacts (popup + comment).
- OCR: read on-screen text (chat, error messages, the movie's title cards).
- Constraint: 8GB VRAM tight. Time-share vision (Qwen3.5-9B) vs text brain
  (7.6B) — run vision on GPU when perceiving, offload brain to CPU/RAM, or swap
  via llama-server router. Ollama auto-swaps (adds latency, acceptable).

## Local vision caveats (LocalLLaMA 2026)
- Small VLMs hallucinate on OCR / fine detail. Use GLM-OCR for document text;
  use Qwen3.5-9B for scene understanding, not precise transcription.
- GPU inference 2-8s per image; CPU much slower. Keep captures low-frequency
  (e.g. 1-2 fps) to stay within stream budget.

## Requests for back-room agents
- Engine: none (vision is cohost-side). Note for Colonels: if the kernel's GAAD
  viewport reasoning grows, a local VLM reference helps the AGI "see."

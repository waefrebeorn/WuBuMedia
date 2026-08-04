SPDX-License-Identifier: WaefreBeorn-UMV3

# Multimodal Swarm 2026 — local model picks (15 heads / 40 hands / 15 ears)

Boss's vision: WuBuDesk runs as a LOCAL SWARM of models, not one. Picks for THIS
rig (RTX 2080 SUPER 8GB sm_75, Ryzen 5 3600 AVX2, 64GB RAM). All open-weight /
local. Full detail: MULTIMODAL_STACK_2026.md.

## TTS (replace edge-tts)
- **Kokoro-82M** (Apache 2.0): 82M, ~2-3GB VRAM or CPU, 54 voices / 8 langs,
  >realtime. DEFAULT cohost voice.
- **Chatterbox** (0.5B, MIT): voice CLONING 7-10s, 23 langs, ~realtime GPU.
  Character voice (clone once a reference take exists).
- CosyVoice2-0.5B (150ms stream), Pocket-TTS (100M, great English) as options.

## STT (replace whisper.cpp — we have the NVIDIA GPU)
- **NVIDIA Canary-Qwen 2.5B**: #1 Open ASR Leaderboard (5.63% WER), EN/DE/FR/ES,
  Apache 2.0 — built for our GPU. Best accuracy.
- **Parakeet TDT 1.1B** (CC-BY-4.0): English, >2000x RTF (blazing; Handy wraps).
- Voxtral Transcribe 2 (5.9% WER, native streaming, 13 langs).
- Whisper Large V3 (MIT) only for 99+ langs.

## Vision (the eyes)
- **Qwen3.5-9B multimodal** (Unsloth UD-Q4_K_XL GGUF + mmproj): FITS 8GB at
  131K ctx, ~60 tok/s. Our rig can run it. Prime local vision brain.
- **GLM-OCR** (LocalAI GGUF): OCR / document-understanding specialist.
- 8GB VRAM tight: time-share vision (GPU) vs text brain (CPU/RAM) or swap via
  router. Small VLMs hallucinate on fine OCR — use GLM-OCR for text.

## Orchestration — the swarm backbone
- **llama.cpp `llama-server` router mode**: multiple local models behind one
  OpenAI-compatible router; each model in its own crash-isolated process.
  Coordinator (WuBuDesk) routes mic→STT→brain→TTS + face; screen→vision→brain.
- Shared AGI-state memory across agents: **Cognee** (graph-vector hybrid,
  temporal, MCP multi-agent) or **Graphiti** (temporal knowledge graph). The
  cohost keeps its own file-based Reflexion log (reflections.json) for
  transparency (ReMe-style).

## Lip-sync (the mouth) — pure browser, zero deps
- WebAudio fftSize=256 (~172Hz/bin): low(0-2.5k)→'oh'/'oo', mid(2.5-10k)→
  'ah'/'ay', high(10-17k)→'ee'/'s'/'sh'; consonants→closed. `mouthOpen =
  min(level*2.5,1)` drives CSS `transform: scaleY()`.
- Or 15KB viseme engine (reddit/webdev 2025): real-time viseme from audio.
- Viseme note: no 1:1 phoneme↔viseme; group /b//p//m/→closed.

SPDX-License-Identifier: WaefreBeorn-UMV3

# WuBuDesk Research Index (everything written down)

Master index of all research + skills produced this session. Boss: "write down
your research and make better tools and better skills." Done.

## Research docs (knowledge/)
- `RESEARCH_INDEX.md` — this file
- `MODEL_LANDSCAPE_2026.md` — 2026 open-weight releases + engine gaps/paces
- `MULTIMODAL_STACK_2026.md` — TTS/STT/Vision best-of-class + swarm orchestration
- `RESEARCH_STEP1_PERSONA.md` — persona/cohost craft, popup triggers, Reflexion
- `RESEARCH_STEP2_VOICE.md` — TTS (Kokoro/Chatterbox) + STT (Canary/Parakeet) + lip-sync
- `RESEARCH_STEP3_VISION.md` — local VLMs (Qwen3.5-9B-VL fits 8GB) + OCR
- `RESEARCH_STEP4_BRAIN.md` — llama-server router swarm + memory (Cognee/Graphiti)
- `RESEARCH_PROGRAM_7STEP.md` — master 7-step iteration project
- `PERSONA_ITERATION_PLAN.md` — 7-step/100-step cohost build plan
- `RESEARCH_LATENCY_SYNC.md` — 7-step A/V latency + sync (barge-in, TTS/STT
  streaming, char-alignment, 8GB resource mgmt, sub-second orchestration)
- `WINDOWS_PORT_NOTES.md` — the 3 MSYS2 GGUF-loader fixes + re-apply recipe
- `COLONELS_AND_STACK.md` — WUBUOS/Colonels honest framing (hosted ZealOS)
- `AGI_MADE_THIS_MOVIE.md` — cohost brief for the film + Visual Language
- `PACE_ENGINE_REQUESTS.md` — gaps found porting (paces for engine/Colonels agents)
- `PACE_SHARED_MEMORY.md` — Cognee/Graphiti shared AGI-state graph request
- `SAMPLING_NOTES.md` — DRY+repeat_penalty loop fix, DeepSeek-V4 pace
- `RESEARCH_PACK.md` — 4-pillar cohost research + public anchors

## Tools (tools/ + src/)
- `tools/wubu_speak.py` — Kokoro TTS -> cohost_line.wav + overlay flag (VERIFIED)
- `tools/wubu_align.py` — ffsubsync + faster-whisper char-alignment (VERIFIED)
- `tools/wubu_serve.py` — llama-server swarm launcher (brain/eyes/coder)
- `tools/wubudesk.py` — cohost CLI (speak/disk/status/verify-model/reflect)
- `src/wubu_speak.py`, `src/wubu_listen.py` (barge-in ears), `src/wubudesk_loop.py`
  (perceive->think->speak coordinator), `src/wubu_obs.py` (OBS WS),
  `src/wubu_see.py` (vision), `src/wubu_desktop.py` (eyes/hands),
  `browser/wubu_bridge.py` + `host/wubu_native_host.py` (WS bridge)
- `start_cohost.bat` — one-click swarm bring-up
- `face/index.html` — wizard overlay (eye-track, blink, mouth, sigil, movie mode)

## Skills (Hermes profile: software-development/)
- `windows-c-porting` — UPDATED: added CUDA/sm_75 nvcc+MSVC port section +
  Python venv (PEP-668/no-pip) section
- `wubudesk-operations` — UPDATED: voice/ears/align/serve tools, full doc index,
  Reflexion loop, multimodal swarm

## Verified facts (Triple-DA)
- CUDA/sm_75: Makefile.win compiles 19 .cu via nvcc -arch=sm_75 (cl.exe on PATH)
- Voice: wubu_speak -> face/cohost_line.wav (24kHz PCM) confirmed
- Align: wubu_align sync (ffsubsync +12.78s on preview) + align pipeline run
- Fleet on disk (valid GGUF): Qwen3.6-27B (Q4_K_M/UD-IQ2_M/Q8_0), Qwen3.5-9B-VL
  +mmproj, KAT-Coder (IQ2_M/Q4_K_M), Agents-A1-4B Q8_0; DeepSeek-V4 ~35/102GB DL
- Overlay :8137 serves HTTP 200

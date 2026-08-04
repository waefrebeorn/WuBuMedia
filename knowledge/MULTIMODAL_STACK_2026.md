SPDX-License-Identifier: WaefreBeorn-UMV3

# Multimodal Stack 2026 — TTS / STT / Vision (the 15 heads, 40 hands, 15 ears)

Researched 2026-08-04. The boss's vision: WuBuDesk runs as a SWARM — many
models, many ways to run them, local-first. This doc picks the 2026 best-of-class
for each modality on THIS rig (RTX 2080 SUPER 8GB sm_75, Ryzen 5 3600, 64GB RAM)
and the orchestration pattern. All open-weight / local.

## TTS (better than edge-tts — local, no cloud)
| Model | Params | VRAM | Speed | Cloning | License | Pick for |
|-|-|-|-|-|-|-|
| **Kokoro-82M** | 82M | 2-3GB / CPU | >RTF (fast) | No | Apache 2.0 | DEFAULT narration — 54 voices, 8 langs, trivial |
| **Chatterbox** | 0.5B | 8-16GB | ~RTF GPU | Yes (7-10s), 23 langs | MIT | Co-host CHARACTER voice (clone the persona) |
| **Fish Speech 1.5 / S1-mini** | — | mid | — | zero-shot | — | Top multilingual (13-80 langs) |
| **CosyVoice2-0.5B** | 0.5B | mid | 150ms stream | cross-lingual | — | Low-latency streaming speech |
| **Pocket-TTS** | 100M | tiny | — | No | — | Amazing English quality, tiny |

Decision: Kokoro-82M as the always-on cohost voice (CPU-friendly, frees GPU for
brain/vision). Chatterbox to give the cohost a *character* voice via cloning when
we have a reference take. Lip-sync still via the 15KB browser viseme engine.

## STT (better than whisper.cpp — local)
| Model | WER | Stream | Langs | VRAM | License | Pick for |
|-|-|-|-|-|-|-|
| **NVIDIA Canary 1B Flash / Canary-Qwen 2.5B** | 5.63% (#1 Open ASR) | batch | EN/DE/FR/ES | NVIDIA GPU | Apache 2.0 | BEST accuracy, we HAVE an NVIDIA GPU |
| **Voxtral Transcribe 2** | 5.9% | native | 13 | — | open | Streaming, lower WER than Whisper |
| **Parakeet TDT 1.1B** | ~8% | >2000x RTF | EN | ~4GB | CC-BY-4.0 | Blazing-fast English (Handy wraps it) |
| **Whisper Large V3** | 7.4% | no | 99+ | — | MIT | Max language coverage |
| **Qwen3-ASR** | comp | — | 52 | — | — | Multilingual alt |

Decision: Canary-Qwen 2.5B for the ears (we have the NVIDIA GPU it's built for,
#1 on the leaderboard). Parakeet/Handy as a fast English fallback. Whisper Large
V3 only if we need 99+ langs.

## Local Vision (the eyes)
- **Qwen3.5-9B multimodal** (Unsloth UD-Q4_K_XL GGUF + mmproj-*.gguf): fits 8GB
  at 131K ctx, ~60 tok/s decode. OUR RIG CAN RUN IT. Prime "eyes" brain.
- **GLM-OCR** (LocalAI GGUF): OCR / document-understanding specialist.
- **Gemma 3 27B**: vision fit for 8GB work.
- Constraint: 8GB VRAM is tight — run vision OR brain, swap (Ollama auto-swaps).
  Plan: eyes use a small vision model (Qwen3.5-9B) on GPU when perceiving; brain
  (7.6B) offloaded to CPU/RAM when vision is active, or time-share.

## Orchestration — the swarm ("15 heads, 40 hands, 15 ears")
- **llama.cpp llama-server router mode**: run MULTIPLE local models behind one
  OpenAI-compatible router; each model in its own process (crash-isolated). This
  is the backbone for TTS+STT+vision+brain as one swarm.
- Pattern: a coordinator (WuBuDesk) routes: mic->STT (Canary) -> brain (LLM) ->
  TTS (Kokoro/Chatterbox) + face overlay; screen->vision (Qwen3.5-9B) -> brain.
- Bonus: Canary/CosyVoice/Kokoro can all be llama.cpp-served (GGUF where avail),
  unifying the stack on one engine family.

## WHAT THIS MEANS FOR THE COHOST
- Step 3 (Voice): swap edge-tts -> **Kokoro-82M** (local, fast) + **Chatterbox**
  for character cloning. No cloud dependency, lower latency, interruptible.
- Step 5 (Perception/ears): swap whisper.cpp -> **Canary-Qwen 2.5B** (we have
  the NVIDIA GPU; #1 leaderboard). 
- Eyes: add **Qwen3.5-9B multimodal** as the vision brain (fits our 8GB!).
- All behind **llama-server router** so 15 heads/40 hands/15 ears run as one swarm.

## SOURCES
- bentoml / localaimaster / ocdevel / siliconflow TTS 2026 roundups
- weesperneonflow / northflank / gladia STT 2026 (Voxtral, Canary, Parakeet, Whisper)
- bentoml / mindstudio / localai.io vision 2026; architecture-performance.fr Qwen3.5-9B 8GB
- llama.cpp GH (llama-server router mode)

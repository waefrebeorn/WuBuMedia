SPDX-License-Identifier: WaefreBeorn-UMV3

# 2026 Local-Model Landscape — what the big players dropped (cohost research)

Researched 2026-08-04. Purpose: know what to pull into the AGI fleet, and spot
gaps our engine (wubuwizard) / the Colonels should close. The cohost (on stage)
turns these into requests for the back-room agents (engine + Colonels).

## THE BIG PLAYERS' 2026 OPEN-WEIGHT DROPS
- **DeepSeek V4 (Apr 24 2026, MIT open weights):**
  - V4-Pro: 1.6T total / 49B active, 61 layers, 1M ctx, up to 384K output.
  - V4-Flash: 284B total / 13-21B active (Config-I quant ~95-102GB), 43 layers,
    1M ctx. Our "Big Kahuna" target. NOTE: HF Config-I build had a ggml type-47
    bug (fixed by our engine agent via legacy-42 remap + TQ dequants).
  - Structural innovation: **DSA (DeepSeek Sparse Attention)**, MLA, mHC
    hyper-connections, hash-routing MoE, token-wise compression. 1M ctx is
    DEFAULT tier (not premium).
- **Qwen3.6 family (Alibaba, open weights):**
  - Qwen3.6-35B-A3B: 35B/3B active MoE, top agentic-coding + multilingual,
    multimodal thinking. Our 35B workhorse + Escha W2 base. (No Qwen3.7 27B
    exists; Qwen3.8 27B expected ~1 week out per boss.)
  - Qwen3.6-27B: dense 27B, flagship-level coding. Our "base model" pick.
  - Community variants: DavidAU NEO-CODE, unsloth/Chadrock/CrownHalo quants.
- **GLM 5.2 (Zhipu):** "Opus-style agentic coding, portable." Chinese open
  frontier leader on peak coding.
- **NVIDIA Nemotron 3 Ultra (550B/55B):** US open-weight accelerator, deepest
  pockets. Trails GLM 5.2 on raw intel.
- **Laguna S 2.1 (Poolside):** 70.2 score @ 118B — punches above weight.
- **Mistral Small 4 (119B), MiniMax M3 (428B), Kimi K3 (2800B), Hy3 (295B),
  Inkling (975B):** varied frontiers.
- **Code specialists:** Mellum 2 12B-A2.5B (JetBrains, LCB v6 69.9), NousCoder
  14B, OmniCoder 9B, Step-3.5-Flash, Devstral Small 2, GPT-OSS-120B.

## INFERENCE ENGINE LANDSCAPE (2026)
- **llama.cpp** still the portable C/C++ reference (GGUF, CPU+GPU, custom CUDA
  kernels, flash-attn, speculative decoding). The yardstick.
- Wrappers: Ollama, LM Studio (both wrap llama.cpp).
- vLLM / SGLang / TGI: server-scale, batching.
- **Key technique we should mirror:** FlashAttention (tiled, on-chip, scales
  with ctx), CUDA Graphs (7x decode win), speculative decoding.
- **Our engine (wubuwizard):** from-scratch SSM+GQA+MoE, IQ2-specialized.
  Gaps vs field: Q8_0 dense not implemented (only IQ2_XXS..IQ4_XS, Q2_K..Q6_K),
  no flash-attn yet, no speculative decoding yet. These are paces for the engine
  agent (see requests below).

## WHAT HELPS US — REQUESTS FOR THE BACK-ROOM AGENTS
(CEO-on-stage -> yell at the people in the back)
1. **Engine agent:** add FlashAttention / tiled attention kernel (DSA-style)
   for long ctx; add speculative decoding; add Q8_0 dense matmul (or document
   it's out of scope). Benchmark vs llama.cpp on our RTX 2080 SUPER sm_75.
2. **Engine agent:** confirm DeepSeek-V4 DSA/mHC/hash-routing is correctly
   mapped in the deepseek4 arch path (research/059 + 4a605d6 landed; verify
   generation quality, not just load).
3. **Colonels agent:** the 1M-token context + sparse-attention efficiency is
   the frontier — note for the kernel's attention subsystem if it grows beyond
   GAAD viewport reasoning.
4. **Fleet:** pull GLM 5.2 + Nemotron 3 Ultra GGUFs when available (newest
   frontiers); they're bigger than our rig but useful as hosted references.

## SOURCES
- deepseek.com V4 release (api-docs.deepseek.com/news/news260424)
- Qwen blog qwen3.6-35b-a3b; Qwen3.6-27B FB post
- openrouter.ai insights "open-weight models that matter June 2026"
- poolside.ai Laguna S 2.1; morphllm best-open-source-llm 2026
- xigh/open-weight-models (curated table, June 2026)
- RedHat llama.cpp vs vLLM; sesamedisk 2026 engines overview; llama.cpp GH

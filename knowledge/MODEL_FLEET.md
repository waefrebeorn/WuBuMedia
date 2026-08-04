SPDX-License-Identifier: WaefreBeorn-UMV3

# Model Fleet — "ethos stages" toward AGI

Boss's framing: every model we run is an **ethos stage** — a step in staged
growth toward AGI. This is the fleet as of 2026-08-04.

## Live brain right now
- **llama-server :57064** serving an Ollama blob: **7.6B, Q4_0, multimodal**
  (completion + vision). This is the MiniCPM-V class — the AGI's perceptual
  brain. It can SEE (we proved eyes->brain: it read the screen + text).
- **Ollama `minicpm-v:latest`** (5.5 GB) — local vision model.

## The fleet (ethos stages)
- **Escha W2** (35B, ~12.3 GB, 2-bit-class hybrid + INT8). "The tiny 35B
  powerhouse." Leads the 35B benchmark field (90/100 HermesAgent-20, 90.9%
  HumanEval+, 75.7% MBPP+). Located: `D:\escha\escha-w2` (safetensors, 3 shards).
  Our RTX 2080 (8 GB) can't GPU-fit it; CPU-offload or a bigger box needed.
- **Qwen3.6-35B-A3B** — the 35B base family (Chadrock/Crown Halo/Strix quants,
  19-36 GB). The workhorse 35B.
- **DeepSeek-V4-Flash Config-I GGUF** = the **"BIG KAHUNA"** (made tiny). 284B
  MoE (21B active), **95-102 GB**, 3 split files <=45 GB. Config-I hybrid
  2.88 bpw. The "mega" model / fake-1T-level AGI growth stage.

## Honesty notes (critical — do not bluff)
- **DeepSeek-V4 is currently BROKEN on HF**: quantized on a branch with wrong
  ggml type ID (47 vs canonical 42); page says "hold off downloading until this
  notice is removed." Needs fork `llama-cpp-turboquant` branch
  `tom/merge-upstream-dsv4`. Stock llama.cpp / wubuwizard CANNOT load it.
- **wubuwizard CANNOT run DeepSeek-V4** (our engine is SSM+GQA+MoE; DeepSeek is
  `deepseek4` arch). DeepSeek needs a llama.cpp-class runner, not wubuwizard.
- **Escha/Qwen are 35B** — wubuwizard's own format is its own; these GGUF/
  safetensors need a llama.cpp-class runner too (wubuwizard is a separate,
  from-scratch engine). Our `gen_text_win` build targets wubuwizard's own
  weights, not these.
- Disk: C: 128 GB free, D: 551 GB, E: 269 GB. Big Kahuna (102 GB) fits D:/E:.

## Upcoming
- **Qwen 3.8 35B** "next week" (boss). Use **Qwen3.6 27B** meanwhile — there is
  NO Qwen 3.7 27B (they skipped it). 3.8 27B release expected ~1 week out.

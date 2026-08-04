SPDX-License-Identifier: WaefreBeorn-UMV3

# Sampling knowledge — killing the looping problem (Agents-A1-4B research)

From a prior agent's paste (tested on Agents-A1-4B, llama.cpp + RTX 5070 Ti 16GB,
Q8/F16). This is "the way" for coherent generation on our models. Our engine
(wubuwizard) must implement the same or it will loop.

## Speed tuning (ub exploration)
- Prefill best at **ub 2048**. Q8_0: 6,937 tok/s prefill, ~71 tok/s decode.
  F16: 6,698 tok/s prefill, ~53 tok/s decode. ub tuning dropped total processing
  of long inputs 9.0s -> 7.8s (~13% better). Decode ~unchanged by ub.

## The looping problem (vanilla = bad)
- Without countermeasures, 3+ identical consecutive sentences/paragraphs:
  Q8: failure 34%, repetition 59%. F16: failure 38%, repetition 66%.

## repeat_penalty alone (llama.cpp default refs only last 64 tokens)
- Q8 repetition 59% -> 34%. F16 66% -> 16%. Helps, not enough on long text
  (64-token window can't catch long-range repetition).

## DRY + repeat_penalty (the fix)
- DRY references the ENTIRE context, detects matches with previously output
  token sequences. Combined with repeat_penalty:
  - Q8 failure 34% -> 3%. F16 38% -> 0%.
  - Q8 repetition 59% -> 12%. F16 66% -> 3%.

## Current settings (llama.cpp, RTX 5070 Ti 16GB) — the way
Q8_0:
  -c 131072 -ngl 99 -fa on -ctk q4_0 -ctv q4_0 -b 8192 -ub 2048
  --jinja --reasoning auto --reasoning-format deepseek
  --temp 0.6 --top-p 0.95 --top-k 20
  --repeat-penalty 1.05 --dry-multiplier 0.5 --dry-base 1.75
  --dry-allowed-length 2 --dry-penalty-last-n -1
F16:
  -c 131072 -ngl 99 -fa on -ctk q4_0 -ctv q4_0 -b 8192 -ub 2048
  --jinja --reasoning auto --reasoning-format deepseek
  --temp 0.6 --top-p 0.95 --top-k 20
  --repeat-penalty 1.1 --dry-multiplier 1.2 --dry-base 1.75
  --dry-allowed-length 2 --dry-penalty-last-n -1

## What wubuwizard must do (engine pace addendum)
1. Implement a **DRY sampler** (full-context repetition detection), not just the
   llama.cpp-style 64-token repeat_penalty.
2. Default samplers for our 35B+/MoE models: temp 0.6 / top_p 0.95 / top_k 20
   + repeat_penalty ~1.05 + DRY (mult 0.5-1.2, base 1.75, allowed-len 2,
   last-n -1).
3. DeepSeek-V4 specifically: temp 1.0 / top_p 0.95 (NOT greedy — greedy loops).
4. ub (batch/prefill micro-batch) default 2048 for long-context prefill speed.

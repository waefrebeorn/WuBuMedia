# wubuwizard on Windows — run attempt + verified blocker (2026-08-04)

Boss directive: "use wubuwizard (was bytropix) to run it bubby" — i.e. run
wubuwizard's engine as the Bonzi/bubby cohost brain.

## What wubuwizard is
`C:/Users/eman5/wubuwizard` — "the BRAIN of the WuBu AGI", from-scratch C11
inference engine. Has `gen_text_win.exe` (prebuilt Windows binary) and
`src/wubu_bonzi.c` (the Bonzi companion core = mood/emotion = "bubby").
STATUS.md (2026-08-04) shows a verified engine: TurboQuant loaders (Q2_0/TQ3_1S/
TQ4_1S, type 42->47 remap), DeepSeek-V4 Config-I load gate, MLA/SSM/MoE, etc.

## Invocation (verified)
`gen_text_win.exe <model_path> "<prompt>" <max_tokens>`  (or MODEL env var)
Models on D: Agents-A1-4B-GGUF, Qwen3.6-27B-GGUF (Q4_K_M/Q8_0/UD-IQ2_M),
KAT-Coder, Qwen3.5-9B-VL, plus the ConfigI brain.

## Triple-DA result: MODEL LOADS, BUT CRASHES ON FIRST FORWARD
Ran gen_text_win.exe on two models. Both parsed + mmap'd + allocated layers
correctly, then segfaulted (exit 139) during the first forward/dequant:

- Agents-A1-4B-Q8_0.gguf: loads 426 tensors / mmap 4264MB, then
  `quantized_matmul_from_q8: unsupported quant type 8` x6 -> exit 139.
- Qwen3.6-27B-Q4_K_M.gguf: loads 851 tensors, allocates all 64 layers
  (SSM/GQA), then exit 139 on first forward.
- Tried `WUBU_NO_AVX=1` (forces scalar dequant path) -> still exit 139.

So: the Windows binary loads GGUFs perfectly but dies in the core
forward/dequant path on this Ryzen/Windows rig. NOT model-specific, NOT fixed
by the AVX-off toggle.

## Scope boundary (boss 08-03)
"main dev owns base elements/engine logic — do NOT modify." The crash is in
wubuwizard's core forward/dequant (base-engine). WuBuDesk is the SUPPORTING
guy. So: I verified + report; I do NOT patch wubu_*.c to fix the core.

The Windows AGI mandate ("port Linux code, MAKE IT RUN ON WINDOWS") squares
against the no-modify rule: a rebuild via Makefile.win recompiles (doesn't
modify logic) but won't fix a logic bug, and I can't confirm the toolchain
without trying. This needs a boss decision:

  (a) main dev fixes the Windows forward/dequant crash in gen_text_win.exe, or
  (b) boss explicitly OKs me to attempt a Makefile.win rebuild (Windows-port
      mandate) — may or may not resolve a logic bug, best-effort, no promises, or
  (c) keep the current Ollama brain @:57064 (cohost already works) and leave
      wubuwizard as "verified loader, crashes on forward" pending (a).

## Current cohost brain is unaffected
The live cohost brain @:57064 is an Ollama blob (separate from wubuwizard) and
keeps working. "Run it bubby via wubuwizard" is an upgrade that is currently
blocked by this engine crash.

# DeepSeek-V4-Flash-ConfigI — status (researched 2026-08-04, verified load + online)

Source: `hf download thetom-ai/DeepSeek-V4-Flash-ConfigI-GGUF --include "*.gguf"`
Downloaded to `D:/models/DeepSeek-V4-Flash-ConfigI` (3 GGUF splits, 96 GB on disk).
Model: deepseek-ai/DeepSeek-V4-Flash-0731, 284B MoE / 21B active, 1M ctx, architecture `deepseek4`.

## Files (verified present, valid GGUF magic)
- `...-00001-of-00003.gguf` — 44.6 GB
- `...-00002-of-00003.gguf` — 44.8 GB
- `...-00003-of-00003.gguf` — 13.0 GB

## Triple-DA verdict: NOT SERVABLE on this rig (three independent blockers, all verified)

### Blocker 1 — the downloaded files are the BUGGY pre-fix revision (2026-08-03)
The HF model page carries a KNOWN-ISSUE banner:
> "These files were quantized on a branch where Q2_0 carries ggml type ID 47; the
> canonical TurboQuant fork uses 42. As a result the current files fail to load
> with an error like `tensor 'blk.0.ffn_gate_inp.weight' has offset X, expected Y`.
> The tensor data is fine, only the type field is wrong. Corrected files are being
> re-uploaded now. Please hold off downloading until this notice is removed."

Our local load error (`invalid ggml type 45`, shifted tensor offsets) matches this
exactly. The data is intact; the type field is wrong. Corrected splits are pending
upstream.

### Blocker 2 — needs a SPECIFIC FORK, not stock llama.cpp (verified online)
The page is explicit:
> "This GGUF uses fork-specific ggml types and requires this exact branch:
>  github.com/TheTom/llama-cpp-turboquant @ tom/merge-upstream-dsv4.
>  Stock llama.cpp and earlier fork tips cannot load it. They lack both the
>  deepseek4 architecture and the type table this file was written against."

Installed `D:/llama.cpp` = build **10254** (old scheme, pre-V4). `--help` shows NO
`deepseek4` arch. So even the corrected ConfigI files will NOT load here — it needs
the TurboQuant fork build. (Stock llama.cpp merged DeepSeek V4 support only at
release b9840+; this rig's binary predates that.)

### Blocker 3 — insufficient RAM (independent, hardware)
Rig has 67 GB physical RAM (MemTotal 67018252 kB). The model is 95 GiB (102 GB)
and "fits and runs on 128 GB unified-memory boxes (DGX Spark, Mac)". Even the
smallest practical STOCK-llama.cpp V4 quant (unsloth UD-IQ2_XXS / antirez IQ2XXS) is
~80 GB — still over 67 GB. No V4-Flash quant fits this rig.

## Conclusion
This brain cannot serve on the 67 GB Windows rig under ANY build: (1) pre-fix files,
(2) needs TurboQuant fork, (3) RAM too small. Fix = a 128 GB+ box (e.g. the Linux
team's machine) or wait for corrected files + build the fork there. NOT closable on
this Windows rig — out of WuBuDesk's supporting scope (base-engine/build work).

## What WOULD work elsewhere (for reference)
- Stock llama.cpp b9840+ can run `unsloth/DeepSeek-V4-Flash-0731-GGUF` (UD-IQ3_S etc.)
  on a 128 GB+ box.
- TheTom/llama-cpp-turboquant @ tom/merge-upstream-dsv4 can run the corrected ConfigI
  files, with `TQ_NO_ROTATE=1` (Metal) / routing TQ3_1S through verified dequant
  (CUDA) for correctness.

## Re-run the test later
`scripts/test_brain_dsv4.bat` launches the model on :57066 without touching the live
brain. Re-run after (a) corrected files land AND (b) a TurboQuant-fork build exists
on a 128 GB+ machine.

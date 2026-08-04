# DeepSeek-V4-Flash-ConfigI — status (verified 2026-08-04)

Source: `hf download thetom-ai/DeepSeek-V4-Flash-ConfigI-GGUF --include "*.gguf"`
Downloaded to `D:/models/DeepSeek-V4-Flash-ConfigI` (3 GGUF splits, 96 GB on disk).

## Files (verified present, valid GGUF magic)
- `DeepSeek-V4-Flash-0731-ConfigI-00001-of-00003.gguf` — 44.6 GB
- `DeepSeek-V4-Flash-0731-ConfigI-00002-of-00003.gguf` — 44.8 GB
- `DeepSeek-V4-Flash-0731-ConfigI-00003-of-00003.gguf` — 13.0 GB

## Triple-DA verdict: NOT SERVABLE on this rig (as-is)
Two independent blockers, both verified:

1. **Format/version mismatch (definitive).** Test-load via
   `D:/llama.cpp/llama-server.exe` (batch `scripts/test_brain_dsv4.bat`, port
   :57066, --mmap -ngl 10 -c 2048) failed at model load:
   ```
   E gguf_init_from_reader: tensor 'blk.0.attn_kv.weight' has invalid ggml type 45. should be in [0, 43)
   E llama_model_load: error loading model
   ```
   The GGUF uses **ggml tensor type 45** (DeepSeek-V4-specific MoE/KV type)
   that the installed llama.cpp build does not implement. Needs a newer
   llama.cpp (one that knows type 45) — that is base-engine work owned by the
   main dev, not the WuBuDesk supporting scope.

2. **Insufficient RAM (independent blocker).** The rig has 67 GB physical RAM
   (MemTotal 67018252 kB). The model is ~102 GB across splits; it cannot be
   fully resident even in system memory, let alone the 8 GB 2080 SUPER VRAM.

## The live brain (:57064) is unaffected
The running cohost brain is a different (smaller, standard-ggml-type) model
served via an Ollama blob — it loads fine because it uses supported tensor
types and fits the GPU. The DeepSeek-V4 download is a separate, larger brain
asset that is parked on D: until (a) llama.cpp supports ggml type 45 and
(b) the rig has >102 GB RAM or a smaller quant is produced.

## Re-run the test later
`scripts/test_brain_dsv4.bat` launches the model on :57066 without touching
the live brain. Re-run after a llama.cpp upgrade to confirm load.

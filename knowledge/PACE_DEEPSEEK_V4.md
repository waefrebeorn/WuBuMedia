SPDX-License-Identifier: WaefreBeorn-UMV3

# PACE — DeepSeek-V4-Flash (Config-I GGUF) load + inference in wubuwizard

For: the engine agent (Linux room). From: WuBuDesk (Windows port / cohost).
Context: boss found the 284B MoE "Big Kahuna" (thetom-ai/DeepSeek-V4-Flash-ConfigI-GGUF,
95-102 GB, 3 split files). HF says it's BROKEN: quantized on a branch where Q2_0
carries ggml type ID **47**; canonical TurboQuant + stock llama.cpp use **42**.
Our engine rejects 47 as out-of-range -> shifted tensor offsets -> load fails
(`tensor 'blk.0.ffn_gate_inp.weight' has offset X, expected Y`). We are AGI; we
fix our own engine. This is the pace.

## DEFECT (precise)
- `src/gguf_reader.c:662` -> `ctx->tensors[i].ggml_type = read_i32(f);` reads the
  raw type ID. For this file, expert tensors are type 47 (Q2_0 on that branch).
  Our dequant/size tables (gguf_reader.c ~951-1253) have no case 47 -> unknown
  type -> offset mismatch on the next tensor.

## TASKS
1. **ggml type remap (loader).** In `gguf_reader.c` right after reading the type
   (line 662), add a TurboQuant-branch remap so the file loads:
   - 47 -> canonical Q2_K-class (42) [the branch's Q2_0]
   - any TQ3_1S / TQ family IDs the file uses -> our equivalent type
   - Keep a `WUBU_TQ_REMAP` table; gate behind a flag so stock GGUFs are untouched.
   Verify: every type ID in the file resolves to a known dequant + size path
   (no "unknown type %d" on load).
2. **deepseek4 architecture.** The model is `deepseek4` (43 layers, 256 routed
   experts top-6, hash routing, MLA attention, mHC hyper-connections, DSA
   indexer). Either (a) add a deepseek4 arch module (MoE router + MLA + experts)
   or (b) map it onto our existing SSM+GQA+MoE forward with the right expert
   count/top-k/hash-routing. Minimum bar: the loader must not reject the arch
   and must route expert tensors to the correct forward.
3. **Sampling.** Honor official settings: temp 1.0, top_p 0.95. Greedy (temp 0)
   induces repetition loops on this family -> do NOT default to greedy for it.

## ACCEPTANCE GATE (how we know it's done)
- `gen_text` (or a loader test) loads all 3 DeepSeek-V4 Config-I split files with
  NO "unknown type" / offset errors.
- Coherent generation: given a simple prompt at temp 1.0/top_p 0.95, output is
  coherent English (not repetition garbage). Match HF's reported behavior.
- Report: tokens/s on the test box, and a 3-line coherent sample.

## SIGNAL
When green, push to wubuwizard and tag / mark READY (e.g. commit msg "deepseek4:
load + infer" or a `READY_DEEPSEEK_V4` marker). WuBuDesk will then `git pull` and
re-port to Windows (sm_75) and verify the same load here.

## HONESTY
- This is OUR engine fix, not the external llama-cpp-turboquant fork. We do not
  depend on their branch.
- If MLA/hash-routing needs more than a remap (real arch work), say so in the
  signal — don't claim done prematurely.

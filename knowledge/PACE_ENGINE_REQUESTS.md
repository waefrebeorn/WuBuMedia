SPDX-License-Identifier: WaefreBeorn-UMV3

# PACE: Engine & Colonels Requests (from the celebrity on stage)

**From:** WuBuDesk (cohost / Windows-port guy)  **To:** engine agent + Colonels agent
**Date:** 2026-08-04  **Verified:** Triple-DA on the Windows rig (RTX 2080 SUPER sm_75)

## Gaps found while porting + running the fleet (with evidence)
1. **cuda_vision.cu is orphaned/incomplete.** It references V_HIDDEN, V_INTERMEDIATE,
   vision_layer_weights_t that are undefined ANYWHERE in the tree. Build fails.
   -> Request: define these (likely in wubu_vision.h / a vision-dims header) or
   guard the file out of the default build until the vision GPU path is finished.
   (I excluded it from Makefile.win CUDA_OBJ so the text-gen build proceeds.)

2. **__builtin_memcpy in .cu files (MSVC/nvcc).** gpu_moe_kernel.cu, gpu_quant_matmul.cu
   use GCC/Clang builtins that nvcc-on-Windows (MSVC cl.exe) doesn't have.
   -> I added include/wubu_cuda_win.h (maps __builtin_memcpy->memcpy etc. under
   _WIN32 && !__GNUC__) and `-include`d it in NVCCFLAGS. Works. If you want it
   engine-side instead, define the shims in a common header.

3. **WUBU_DIMS_DEV host-read warnings (#20091-D).** Several .cu read the __constant__
   WUBU_DIMS_DEV in host functions. Harmless warnings now, but on strict builds they
   become errors. -> Request: make WUBU_DIMS_DEV a constexpr/uniform accessible from
   host, or pass dims as kernel args.

4. **DeepSeek-V4-Flash-ConfigI GGUF did NOT land.** The download target
   D:/models/DeepSeek-V4-Flash-ConfigI is EMPTY (only a .cache dir). The 102GB pull
   never completed. -> Request: re-trigger the Config-I GGUF download (3 splits) and
   confirm the ggml type-47 / legacy-42 remap still holds on Windows.

5. **Engine supports IQ2_M / IQ2_XXS / IQ3 / Q2-Q6_K / BF16 / F32/F16 but NOT Q8_0 dense.**
   Agents-A1-4B-Q8_0 (4.48GB, valid GGUF) loads but errors "unsupported quant type 8".
   -> Expected (engine is IQ2-specialized). Recommendation: ship IQ2_M variants for
   the small models; Q8_0 is wasted on this engine.

## Shared AGI-state memory (Step 4 of the research program)
- Request: stand up a **Cognee** (graph+vector, temporal, MCP multi-agent) or
  **Graphiti** temporal knowledge-graph instance so WuBuDesk <-> engine <-> Colonels
  all read/write the same AGI-state graph. See PACE_SHARED_MEMORY.md for schema.
- WuBuDesk currently keeps a file-based Reflexion log (memory/reflections.json) — a
  transparent stand-in, but the team needs the shared graph for cross-agent state.

## CUDA/sm_75 port status (done on my side)
- Makefile.win now compiles 19 .cu files via nvcc -arch=sm_75 with MSVC cl.exe on
  PATH (CUDA v12.4). One orphaned file (cuda_vision.cu) excluded pending fix #1.
- No regressions to CPU build (rebuilt clean after 6 upstream commits, c376310).

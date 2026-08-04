SPDX-License-Identifier: WaefreBeorn-UMV3

# wubuwizard Windows port — fix log (re-apply after pull)

WuBuDesk's Windows port of wubuwizard (my "cave"). When the boss says
"pull + redo," re-apply these three fixes on the fresh tree. The port files
(Makefile.win, include/wubu_win.h, src/wubu_win.c, src/wubu_spawn_win.c,
include/win32/*) are untracked and survive ff-pull. These PATCHES to tracked
files must be re-applied.

## Fix 1 — gguf_reader.c: fstat/fseeko fail on MSYS stdio (line ~1451)
`gguf_buffer_data()` used `fstat(fileno(ctx->file), &st)` to get file size;
on MSYS2/mingw stdio this fails (and fseeko SEEK_END also fails). Replaced
the `_WIN32` branch with `_filelengthi64(fileno(ctx->file))` (no seek needed).
```
#if defined(_WIN32)
    long long fsz = _filelengthi64(fileno(ctx->file));
    if (fsz < 0) { fprintf(stderr,"gguf_buffer_data: _filelengthi64 failed\n"); return 0; }
    file_size = (uint64_t)fsz;
#else
    if (fstat(fileno(ctx->file), &st) != 0) { ... }
    file_size = (uint64_t)st.st_size;
#endif
```

## Fix 2 — wubu_win.c: mmap on read-only file (wubu_mmap, file-backed branch)
Hardcoded PAGE_READWRITE/FILE_MAP_WRITE; a "rb"-opened file can't get a
writable mapping. Honor `prot`: use PAGE_READONLY/FILE_MAP_READ unless
PROT_WRITE. Also added GetLastError() logging on MapViewOfFile failure.

## Fix 3 — wubu_win.h: sysconf(_SC_PAGESIZE) must be 65536 on Windows
Returned `si.dwPageSize` (4096). Windows MapViewOfFile requires the mapping
OFFSET aligned to allocation granularity (65536), not 4KB page size. Changed
to `si.dwAllocationGranularity`. Without this, any >file-aligned mmap fails.

## Result (verified 2026-08-04, RTX 2080 SUPER sm_75, Ryzen 5 3600)
- `gen_text_win` builds (CPU+shim, no CUDA kernels yet) and RUNS.
- Loaded Agents-A1-4B Q8_0 (4.2 GB GGUF): mmap'd 4264 MB, tokenizer inited
  (248320 tokens), membudget + hwaccel detected, ran, exited 0.
- NOTE: Q8_0 dense weights hit `unsupported quant type 8` — our engine
  (SSM+GQA+MoE, IQ2-specialized) does NOT implement Q8_0 dense matmul.
  EXPECTED. The right test model is the IQ2_M / mixed-quant family
  (Qwen3.6-27B UD-IQ2_M, KAT IQ2_M) — those are the engine's specialization.
- Engine supported quants: F32/F16/BF16, IQ2_XXS, IQ3_XXS, IQ4_XS,
  Q2_K–Q6_K. NOT Q8_0.

## Re-apply recipe after ff-pull
1. Keep untracked port files (they survive).
2. `patch` Fix 1 into src/gguf_reader.c (search "fstat failed").
3. `patch` Fix 2 into src/wubu_win.c (search "File-backed").
4. `patch` Fix 3 into include/wubu_win.h (search "_SC_PAGESIZE").
5. `make -f Makefile.win gen_text_win -j4`
6. To test: `python3 python/extract_tokenizer.py <model.gguf> data` then
   `./gen_text_win <model.gguf> "prompt" 24`.

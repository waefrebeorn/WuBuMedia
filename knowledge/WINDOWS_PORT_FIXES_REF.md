SPDX-License-Identifier: WaefreBeorn-UMV3

# Windows Port — the 3 GGUF-loader fixes (re-apply after git pull)

These fixes made `gen_text_win` (wubuwizard, MSYS2/mingw64, CUDA 12.4) load a
4.2 GB GGUF on Windows. "Own little cave" protocol: stash Win-port files,
`git pull --ff-only`, re-apply, rebuild, gdb-verify. All gated `#ifdef _WIN32`.

## Fix 1 — file size (src/gguf_reader.c, gguf_buffer_data)
MSYS `fstat(fileno(ctx->file), &st)` fails on the stdio handle. Use the fd
directly via `_filelengthi64` (needs `<io.h>`, pulled in via force-included
wubu_win.h):
```c
#if defined(_WIN32)
    long long fsz = _filelengthi64(fileno(ctx->file));
    if (fsz < 0) { fprintf(stderr,"gguf_buffer_data: _filelengthi64 failed\n"); return 0; }
    file_size = (uint64_t)fsz;
#else
    struct stat st;
    if (fstat(fileno(ctx->file), &st) != 0) { ... return 0; }
    file_size = (uint64_t)st.st_size;
#endif
```

## Fix 2 — mmap PROT (src/wubu_win.c, wubu_mmap)
A read-only `fopen("rb")` file can NOT get a READWRITE mapping. Honor `prot`:
```c
DWORD prot = PAGE_READONLY, map_access = FILE_MAP_READ;
if (prot & PROT_WRITE) { prot = PAGE_READWRITE; map_access = FILE_MAP_WRITE; }
HANDLE map = CreateFileMappingA(fh, NULL, prot, 0, 0, NULL);
void *view = MapViewOfFile(map, map_access, 0, (DWORD)off, len);
```

## Fix 3 — allocation granularity (include/wubu_win.h, wubu_sysconf)
Windows `MapViewOfFile` needs the offset aligned to **allocation granularity
(65536)**, NOT the 4096 page size. Return the granularity or the map fails
silently:
```c
if (name == _SC_PAGESIZE) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (long)si.dwAllocationGranularity;  // 65536, not dwPageSize
}
```

## Build + verify (proven)
```bash
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
cd /c/Users/eman5/wubuwizard
make -f Makefile.win gen_text_win -j4
gdb -batch -ex run --args ./gen_text_win <model.gguf> "prompt" 24   # gdb: clean env
python3 python/extract_tokenizer.py <gguf> data                     # vocab for tokenizer
```
Engine supports IQ2_XXS/IQ3_XXS/IQ4_XS/Q2_K–Q6_K/BF16 — NOT Q8_0 dense.
Test with IQ2_M family (our specialization), not Q8_0.

## Pitfalls
- Don't "simplify" the 3 fixes away — each is required on MSYS2.
- A direct subprocess crash (0xC0000005) is an env artifact (missing MSYS/CUDA
  DLL PATH), NOT a real bug. Verify under gdb.
- `make` may not recompile on a changed `-include` header — `rm` the .o / touch a
  TU to force it.

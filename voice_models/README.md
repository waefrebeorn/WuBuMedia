# WuBuMedia Voice Models

AI voice model catalog and inference engine — organized by cultural popularity.

## Structure

```
WuBuMedia/
├── voice_models/          # Voice model catalog and downloads
│   ├── catalog/           # Curated voice catalog
│   │   ├── local/         # 21 local RVC models (.pth + .index)
│   │   ├── vm_index/      # 8,766 ranked VM models with download URLs
│   │   └── voice_catalog.json  # Full 500-voice ranked catalog
│   ├── downloaded/        # Downloaded models (symlinks to D:\1aivoice)
│   │   ├── ranked_1000/   # Top 1000 most popular models
│   │   ├── ranked_2000/   # Next 1000 most popular
│   │   ├── ranked_3000/   # ...
│   │   └── overflow/      # 766 models beyond 8000
│   ├── research/          # Research data (BAFTA, MyAnimeList, SocialBlade)
│   └── tests/             # A/B test results and verification
├── src/                   # Source code
│   ├── wubu_rvc.py        # WuBuMedia RVC integration
│   └── wubu_rvc.c         # C11 RVC inference engine
├── build/                 # Compiled binaries
│   ├── test_pipeline.exe
│   ├── test_rvc_compare.exe
│   ├── test_speed_real.exe
│   └── test_quality.exe
├── tools/                 # CLI tools
│   ├── curate_voice_catalog.py  # VOCAB + popularity scorer
│   ├── crossref_vm.py          # Cross-reference with 30K VM index
│   ├── organize_models.py      # Organize into D: drive batches
│   ├── acquire_models.py       # Download models 1000 at a time
│   ├── ab_test_rvc.py          # A/B test runner
│   └── generate_ab_video.py    # A/B test video generator
├── out/                   # Generated outputs
│   ├── voices.json          # 500 curated voices with URLs
│   └── vm_models_ranked.json  # 8766 ranked VM models
└── outputs/               # Test outputs
    ├── ab_test_stats.json      # A/B test results
    ├── ab_test_stats.png       # Stats overlay image
    └── ab_test_video.mp4       # A/B test video
```

## Storage

All downloaded models are stored on the **D: drive** (1.9TB), not C:.
- D:\1aivoice\VoiceModels\ranked_1000/  → Top 1000 most popular models
- D:\1aivoice\VoiceModels\ranked_2000/  → Next 1000
- D:\1aivoice\TrainingData/             → Models with training data (93)
- D:\1aivoice\ModelWeights/             → Pure .pth weights (4713)
- D:\1aivoice\ArchiveBundles/           → Zip/rar bundles (3960)

## Engine

**WuBuRVC** — our custom RVC inference engine in C11 with CUDA kernels (sm_75).
- `src/wubu_rvc.c`         — Main engine (HiFi-GAN generator)
- `src/wubu_rvc_kernels.cu` — CUDA kernels for fused operations
- `src/wubu_rvc_mono.cu`    — Monophonic RVC conversion kernel
- Build: `cc -Wall -std=c11 -g -I src src/*.c -lm -o build/engine`

**PyTorch Reference** — Mangio-RVC fork reference implementation.
- `tools/gen_reference_pytorch3.py` — HiFiGAN generator in PyTorch

## Usage

### Run A/B test
```bash
python tools/ab_test_rvc.py
python tools/generate_ab_video.py
```

### Download models
```bash
python tools/acquire_models.py 1   # Top 1000 models
python tools/acquire_models.py     # All 8 batches
```

### Run inference
```bash
build/test_pipeline.exe    # Full pipeline test
build/test_rvc_compare.exe # Accuracy comparison
build/test_speed_real.exe  # Speed benchmark
```

License: WaefreBeorn-UMV3

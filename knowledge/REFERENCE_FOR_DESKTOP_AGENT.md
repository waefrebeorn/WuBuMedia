# WuBuRVC Reference Summary for Desktop Agent

**Date:** 2026-08-07  
**Author:** WuBuDesk  
**Status:** ✅ Complete — all reference materials collected

## 1. Existing C11 Codebase (in `src/`)

The desktop agent already has a complete C11 RVC inference engine. Here's what exists:

### Core Engine Files
```
src/
├── wubu_rvc.h/c           — Public engine API (frame buffer, RVCGraph IR, kernel dispatch)
├── wubu_rvc_real.h/c      — Full pipeline: enc_p → flow → GeneratorNSF (1142 lines)
├── wubu_rvc_parity.h/c    — Parity harness + model loading infrastructure
├── wubu_rvc_weights.c     — Flat-binary weight loader + weight_norm de-norm
├── wubu_rvc_kernels_exact.c  — Exact HiFi-GAN transposed conv + MRF kernel
├── wubu_rvc_hubert.c/h    — HuBERT content encoder
├── wubu_rvc_f0.c/h        — F0 extraction (YIN fallback, no RMVPE)
├── wubu_rvc_kernels.cu    — CUDA kernels for sm_75
└── wubu_rvc_cli.c         — CLI application
```

### Key Data Structures (from `wubu_rvc.h`)
```c
typedef struct {
    char name[128];      // tensor name
    float *data;         // tensor data
    int n_dims;
    int dims[4];
    int offset;
} RVCTensor;

typedef struct WuBuRVCModel {
    RVCTensor *tensors;
    int n_tensors;
    int version;         // RVC_V1=1, RVC_V2=2
    int sample_rate;     // 40000 for Cartman v2
    int hidden_channels; // 256
    float *hifi_upsample[4];             // de-normalized ups weights
    float *hifi_upsample_denorm[4];      // cached denorm weights
    float *hifi_mrf0, *hifi_mrf1;        // MRF resblock weights
    float *vocoder_conv_pre;             // noise_conv weights
    float *vocoder_conv_post;            // conv_post weights
    float *weight_blob;                  // entire model in flat buffer
    int loaded;
} WuBuRVCModel;
```

## 2. Python Reference Implementations (cloned to `knowledge/`)

### 2.1 Original RVC (`knowledge/rvc_original/`)
**Repo:** https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI

Key files for C11 porting:
- **`infer/module/models.py`** — Contains `Generator` (HiFi-GAN), `GeneratorNSF`, `TextEncoder`, `ResidualCouplingBlock`
- **`infer/module/modules.py`** — Contains `ResBlock1`, `ResBlock2`, `WN` (WaveNet), `DDSConv`
- **`infer/module/commons.py`** — Contains `init_weights`, `get_padding`, `fused_add_tanh_sigmoid_multiply`, `kl_divergence`, `sequence_mask`
- **`infer/module/attentions.py`** — Contains `Encoder` (1D conv self-attention, 2 heads, window=10)
- **`infer/module/transforms.py`** — Contains `Log`, `Flip`, coupling layers
- **`infer/vc/pipeline.py`** — Full inference pipeline (HuBERT → FAISS → F0 → enc_p → flow → GeneratorNSF)

### 2.2 Mangio-RVC Fork (`knowledge/rvc_mangio/`)
**Repo:** https://github.com/Mangio621/Mangio-RVC-Fork

Key files:
- **`lib/infer_pack/models.py`** — HiFiGAN Generator with weight_norm (same as original)
- **`lib/infer_pack/modules.py`** — ResBlock1, ResBlock2, WN (same architecture)
- **`lib/infer_pack/attentions.py`** — Encoder
- **`rmvpe.py`** — RMVPE pitch extractor (U-Net F0)

### 2.3 Applio-style reference (in `tools/`)
- **`tools/gen_reference_pytorch3.py`** — PyTorch reference generator for parity testing
  - Defines `HiFiGANGenerator` class (exact copy of Applio/hifigan.py)
  - `load_reference()` function — loads .pth and strips `dec.` prefix
  - Config mapping: `h[3]` = inter_channels, `h[10]` = resblock_kernel_sizes, `h[11]` = dilations, `h[12]` = upsample_rates, `h[13]` = upsample_init, `h[14]` = upsample_kernel_sizes

### 2.4 Existing parity test references (in `WuBuMedia/` root)
- `pytorch_ref_output.npy` — Reference output (6528 floats)
- `pytorch_ref_mel.npy` — Reference mel input (4×80 = 320 floats)
- `pytorch_ref_stats.txt` — Reference output statistics
- `verify_conv1d_w.npy`, `verify_conv1d_b.npy`, `verify_out.npy`, `verify_mel.npy`, `verify_w.npy` — Layer-by-layer verification arrays

## 3. Architecture: RVC v2 HiFi-GAN Generator (Cartman model)

**Config (from .pth):** `[1025, 32, 192, 192, 768, 2, 6, 3, 0, '1', 512, ...]`

```
Input: mel (192 channels, n_frames)
├── conv_pre: Conv1d(192→512, k=7, s=1, p=3)
├── ups.0: ConvTranspose1d(512→256, k=16, s=10, p=3) → MRF stage 0
│   ├── resblocks 0-2: ResBlock1(256, k∈{3,7,11}, d∈{1,3,5}) — 3 stacks averaged
├── ups.1: ConvTranspose1d(256→128, k=16, s=10, p=3) → MRF stage 1
│   ├── resblocks 3-5: ResBlock1(128, k∈{3,7,11}, d∈{1,3,5})
├── ups.2: ConvTranspose1d(128→64, k=4, s=2, p=1) → MRF stage 2
│   ├── resblocks 6-8: ResBlock1(64, k∈{3,7,11}, d∈{1,3,5})
├── ups.3: ConvTranspose1d(64→32, k=4, s=2, p=1) → MRF stage 3
│   ├── resblocks 9-11: ResBlock1(32, k∈{3,7,11}, d∈{1,3,5})
└── conv_post: Conv1d(32→1, k=7, s=1, p=3) → tanh
```

**Weight normalization:** Every conv layer uses `weight_norm(weight_v, weight_g)` → de-normalize:
```python
W = weight_g * (weight_v / ||weight_v||)
```
Our `wubu_rvc_denormalize_weight()` implements this correctly (verified: max delta ~5e-6 vs PyTorch).

**Conv1d weight layout (C-order):** `(out_ch, in_ch, k)` → flat index: `oc * in_ch * k + ic * k + tap`  
**ConvTranspose1d weight layout:** `(in_ch, out_ch, k)` → flat index: `ic * out_ch * k + oc * k + tap`

## 4. Known Issues in Existing C11 Code

From `knowledge/WUBU_RVC_SPEED_PARITY_TEST.md`:

1. **Jitter uses wrong formula**: `sin(2πt)` instead of `sin(2πf·t)`
2. **Flow forward only does 1 step** instead of `n_flows * 2 = 8`
3. **Mel extraction doesn't zero-pad correctly** for the first frame
4. **conv1d uses stride=1 hardcoded** — works for RVC but not general
5. **Output truncation**: `GeneratorNSF.forward` truncates to `x.shape[-1] * self.upp` instead of `audio_len / hop`

## 5. What the Desktop Agent Is Currently Doing

Based on the skills and tools available, the desktop agent is:

- **`wubu_rvc_voices.py`** — Converting voice catalogs (voice-models.com, etc.) into RVC-compatible format
- **`wubu_rvc_training.py`** — C11 training + fine-tuning infrastructure (HuBERT, RMVPE, AnyPrecisionAdamW, bf16)
- **`wubu_rvc_voice_engine.py`** — Real-time voice engine (mic → RVC → OBS/Voicemeeter)

## 6. Immediate Next Steps for the Agent

1. **Download training assets:** `hubert_base.pt`, `rmvpe.pt`, `fcpe.pt`, G256k base model
2. **Fix jitter**: `sin(2π * f0 * t)` — `f0` is per-frame, `t` is sample index
3. **Fix flow layers**: Iterate `n_flows * 2` coupling layers (8 total for v2)
4. **Rewrite `wubu_kernel_hifigan`**: Use `wubu_rvc_kernels_exact.c` conv_transpose + MRF instead of linear interpolation
5. **Add RMVPE**: Port the U-Net from `knowledge/rvc_original/infer/rmvpe.py` to C11
6. **Add HuBERT**: Port HuBERT from `knowledge/rvc_original/infer/hubert.py` (12-layer transformer + CNN feature extractor)
7. **Add training loop**: Loss computation (L1 + feature matching + KL), gradient descent

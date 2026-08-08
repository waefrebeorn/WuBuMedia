# WuBuRVC — Reference Code Index for C11 Port

**Last updated:** 2026-08-07  
**License:** WaefreBeorn-UMV3

This document indexes all reference implementations available to the WuBuRVC C11 port. The desktop agent should use this to navigate both the existing codebase and the cloned Python reference implementations.

---

## 1. Existing C11 Codebase (`src/`)

| File | Size | Description |
|------|------|-------------|
| `src/wubu_rvc.c` | 22KB | Core engine: frame buffer, RVCGraph IR, CPU kernels (autonorm, flow_couple, hifigan, vocoder) |
| `src/wubu_rvc.h` | 7.5KB | Public header for wubu_rvc.c |
| `src/wubu_rvc_kernels.cu` | 10KB | CUDA kernel versions (sm_75) |
| `src/wubu_rvc_kernels_exact.c` | 15KB | Exact HiFi-GAN kernel with real transposed conv |
| `src/wubu_rvc_real.c` | 56KB | Full pipeline: enc_p + flow + GeneratorNSF |
| `src/wubu_rvc_real.h` | 3.7KB | Header for real.c |
| `src/wubu_rvc_hubert.c` | 29KB | HuBERT content encoder |
| `src/wubu_rvc_hubert.h` | 3.4KB | HuBERT header |
| `src/wubu_rvc_f0.c` | 2.4KB | F0 extraction (YIN fallback) |
| `src/wubu_rvc_f0.h` | 1.6KB | F0 header |
| `src/wubu_rvc_weights.c` | 10KB | WUBU binary loader + weight_norm de-norm |
| `src/wubu_rvc_cli.c` | 11KB | CLI app |
| `src/wubu_rvc_parity.c` | 32KB | Parity harness (C11 vs PyTorch) |
| `src/wubu_rvc_parity.h` | 14KB | Parity header |
| `src/wubuvc.c` | 15KB | CLI app (legacy) |

### Build command
```bash
gcc -O3 -march=native -fopenmp -std=c11 -I src -o build/test_rvc_real.exe \
    src/test_rvc_real.c src/wubu_rvc.c src/wubu_rvc_parity.c \
    src/wubu_rvc_weights.c src/wubu_rvc_kernels_exact.c \
    src/wubu_rvc_real.c src/wubu_rvc_hubert.c src/wubu_rvc_f0.c \
    -lsqlite3 -lm -fopenmp
```

### Current verified parity (Cartman, 2.8s input)
- **HuBERT content:** PASS — max abs diff 3e-5 vs PyTorch
- **Synth audio:** PASS — SNR 29.78 dB vs Mangio-RVC reference

### Known issues from existing code
1. `wubu_rvc_jitter` in `wubu_rvc_real.c` uses `sin(2πt)` but PyTorch reference uses `sin(2πf·t)`
2. `wubu_rvc_hubert_layers` hardcodes `T/2` padding
3. Flow `wubu_flow_forward` only does 1 step (should do `n_flows * 2` = 8)
4. Kernel `wubu_kernel_hifigan` uses linear interpolation instead of exact transposed conv
5. `GeneratorNSF.forward` truncates output to `x.shape[-1] * self.upp` instead of `audio_len / hop`

---

## 2. Cloned Reference Repositories

### 2.1 Original RVC (`knowledge/rvc_original/`)
- **Repo:** https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI
- **License:** MIT-style (see repo LICENSE)

| Path | Purpose |
|------|---------|
| `infer/module/models.py` | Generator, GeneratorNSF, TextEncoder, ResidualCouplingBlock |
| `infer/module/modules.py` | ResBlock1, ResBlock2, WN, DDSConv, Flip, Log, ElementwiseAffine |
| `infer/module/commons.py` | init_weights, get_padding, fused_add_tanh_sigmoid_multiply, kl_divergence |
| `infer/module/attentions.py` | Encoder (1D-ConSelfAttention + convffn) |
| `infer/module/transforms.py` | PiecewiseRationalQuadraticCoupling (flow transform) |
| `infer/hubert.py` | HuBERT feature extraction (feature 768-dim, 300 → 768) |
| `infer/rmvpe.py` | RMVPE pitch extraction (U-Net F0) |
| `infer/audio.py` | STFT, mel, librosa utilities |
| `infer/vc/pipeline.py` | Full inference pipeline (F0, HuBERT, FAISS retrieval, synthesis) |
| `configs/v2/40k.json` | RVC v2 40k config (default) |
| `configs/v2/48k.json` | RVC v2 48k config |

### 2.2 Mangio-RVC Fork (`knowledge/rvc_mangio/`)
- **Repo:** https://github.com/Mangio621/Mangio-RVC-Fork
- **License:** See repo LICENSE

| Path | Purpose |
|------|---------|
| `lib/infer_pack/models.py` | HiFiGAN Generator with weight_norm (same architecture) |
| `lib/infer_pack/commons.py` | Math utilities (init_weights, get_padding, etc.) |
| `lib/infer_pack/transforms.py` | Flow transforms (log, flip, residual coupling) |
| `lib/infer_pack/attentions.py` | Encoder (1D conv self-attention + feedforward) |
| `lib/infer_pack/modules.py` | ResBlock1, ResBlock2, WN (WaveNet-style) |
| `rmvpe.py` | RMVPE U-Net pitch extractor |

---

## 3. Key Architecture Details

### RVC v2 Generator (HiFi-GAN + NSF)
**Source:** `knowledge/rvc_original/infer/module/models.py` (class `Generator` and `GeneratorNSF`)

```
GeneratorNSF:
  conv_pre: Conv1d(192, 512, 7, 1, padding=3)
  ups[0]: ConvTranspose1d(512→256, k=20, stride=8, padding=6)
  ups[1]: ConvTranspose1d(256→128, k=4,  stride=8, padding=-2)
  ups[2]: ConvTranspose1d(128→64,  k=4,  stride=2, padding=1)
  ups[3]: ConvTranspose1d(64→32,   k=4,  stride=2, padding=1)
  resblocks[i*4+j]: ResBlock1(ch, k, d) for k∈{3,7,11}, d∈{1,3,5}×4
  conv_post: Conv1d(32, 1, 7, 1, padding=3, bias=False)
  noise_convs: Conv1d(1, ch, kernel=stride_f0*2, stride=stride_f0, padding=stride_f0//2)
```

**Default RVC v2 40k config:**
- initial_channel (phone): 192 (256 for v1)
- upsample_rates: [8, 8, 2, 2]
- upsample_kernel_sizes: [20, 4, 4, 4]
- upsample_initial_channel: 512
- resblock_kernel_sizes: [3, 7, 11]
- resblock_dilation_sizes: [[1,3,5], [1,3,5], [1,3,5]]
- resblock: "1" (ResBlock1)

### Weight Normalization
**Source:** `knowledge/rvc_original/infer/module/commons.py`

PyTorch applies `weight_norm` to Conv1d/ConvTranspose1d. The de-normalization formula:
```
weight_norm(w) = w / ||w||_2 * g
  where g = ||w||_2 (scalar gain)

To invert (de-normalize):
  w = w_orig * ||w_orig||_2  ... but torch stores:
  w_v = weight (direction)
  w_g = norm (scalar)
  actual_weight = w_v / ||w_v|| * w_g
```

### HiFi-GAN ResBlock (ResBlock1)
**Source:** `knowledge/rvc_original/infer/module/modules.py`

Each ResBlock1 has:
- convs1: 3 Conv1d layers with kernel_size, dilations=(1,3,5), weight_norm applied
- convs2: 3 Conv1d layers with kernel_size, dilation=1, weight_norm applied
- forward: leaky_relu(0.1) → conv1 → leaky_relu(0.1) → conv2 → x + residual

### Fused Add Tanh Sigmoid Multiply (WN module)
**Source:** `knowledge/rvc_original/infer/module/commons.py`

```python
def fused_add_tanh_sigmoid_multiply(input_a, input_b, n_channels):
    n_channels_int = n_channels[0]
    in_act = input_a + input_b
    t_act = torch.tanh(in_act[:, :n_channels_int, :])
    s_act = torch.sigmoid(in_act[:, n_channels_int:, :])
    acts = t_act * s_act
    return acts
```

### Transposed Convolution (Upsampling)
**Source:** PyTorch `nn.ConvTranspose1d` docs

```
ConvTranspose1d(in_ch, out_ch, kernel_size=k, stride=s, padding=p):
  output_len = (input_len - 1) * stride - 2 * padding + kernel_size
  For output padding=0:
  output[t] = sum over j of input[j] * weight[out_ch, in_ch, t*s - j*p_adjust + ???]
```

### Pipeline Flow
**Source:** `knowledge/rvc_original/infer/vc/pipeline.py`

1. Load HuBERT model → extract content features (768-dim, 25fps)
2. FAISS retrieval: search 30k-vector index, weighted blend with source
3. Content ×2 (interpolate to target sr)
4. F0 extraction (RMVPE/FCPE/CREPE) → pitch coarsification (256 bin)
5. enc_p: content + pitch embedding → posterior encoder
6. Flow: posterior encoder → Gaussian mixture → z (8 flows)
7. GeneratorNSF: z + f0 → audio waveform (40k or 48k)

---

## 4. Weight Key Names in .pth Files

```
conv_pre.weight, conv_pre.bias
ups.{i}.weight, ups.{i}.weight_g, ups.{i}.weight_v, ups.{i}.bias
resblocks.{i}.convs1.{j}.weight, resblocks.{i}.convs1.{j}.weight_g, resblocks.{i}.convs1.{j}.weight_v, resblocks.{i}.convs1.{j}.bias
resblocks.{i}.convs2.{j}.weight, resblocks.{i}.convs2.{j}.weight_g, resblocks.{i}.convs2.{j}.weight_v, resblocks.{i}.convs2.{j}.bias
conv_post.weight, conv_post.bias
noise_convs.{i}.weight, noise_convs.{i}.bias
emb_pitch.emb_weight (256 x hidden) — v2 only
emb_phone.weight (in_channels x hidden)
flow.{i}. layers (affine coupling, 8 layers for v2)
```

---

## 5. Training Assets Needed

| Asset | Purpose | Status |
|-------|---------|--------|
| `hubert_base.pt` | HuBERT content encoder weights | 🟡 Not downloaded |
| `rmvpe_2026_full_24000_256x96x4x3.pt` | RMVPE pitch extraction | 🟡 Not downloaded |
| `fcpe_2026.pt` | FCPE pitch alternative | 🟡 Not downloaded |
| G256k base model | HiFi-GAN pre-trained generator | 🟡 Not downloaded |

**Download locations:** See `tools/download_training_assets.py`

# WuBuRVC "Robot Voice" Root Cause — CLI Pipeline Bugs (2026-08-07 v2)

The boss reported Cartman sounded "robot and wrong" on ALL A/B samples.
Investigation (Triple-DA) found the CLI synthesis pipeline diverged from the
verified-correct reference path (`test_rvc_real` = PyTorch Mangio, corr
0.9999, SNR 29.7 dB) in **three** ways. Two are fixed; the third is the
f0-extractor gap (YIN vs RMVPE) which needs an RMVPE/FCPE port.

## Bug 1 (FIXED): f0 timeline compressed 2× against content

The CLI computed YIN f0 at 100 fps, then did `×2 nearest` to 200 fps and
TRUNCATED to the content length (278). Because `test_rvc_real` passes f0
1:1 with the ×2 content (both 278 frames @ 100 fps), the CLI's truncated
×2 array = `repeat(f0, 2)[:278]` — the pitch contour played at HALF speed
relative to the speech. Verified: cli nsff0 was 556 frames vs ref 278.

**Fix:** remove the ×2. RVC consumes f0 at the same frame rate as the
nearest-×2 content (100 fps). Both YIN and `--f0ref` paths now pass f0 1:1.

## Bug 2 (FIXED): content ×2 upsample used linear, not nearest

The CLI's `upsample_frames` used linear interpolation with
`align_corners=True`. The reference generation used
`F.interpolate(feats, scale_factor=2)` on a 3D tensor — default mode
**nearest**. Verified: `np.repeat(content, 2, axis=0)` matches
`content_up.npy` with **max diff 0.0**.

**Fix:** `upsample_frames` now repeats each frame twice (nearest).

## Bug 3 (OPEN): YIN f0 vs training-time RMVPE f0

After fixing 1+2, the CLI with reference f0 produces output correlating
**0.9999** with the PyTorch reference — the pipeline is correct. But YIN
f0 vs RMVPE f0 land in the same coarse bin only **22.8%** of voiced frames
(mean bin diff 1.71, 49% off by 2+ bins). The flow conditioning consumes
coarse bins (emb_f0 lookup), so ~77% of frames get a different conditioning
than training → the voice is subtly but pervasively wrong.

YIN Hz correlation is 0.971 / 6 Hz mean diff — but bin-level agreement is
what matters for the model. RMVPE (rmvpe.pt, 181 MB, U-Net + bidirectional
GRU, 741 tensors) is the extractor the reference used; FCPE (fcpe.gguf,
43 MB) is the modern RVC default. Porting one of them to C11 is the
definitive fix (RMVPE = exact match for this model).

## Status

- CLI fixes committed: nearest content upsample + f0 1:1 (no ×2).
- `--f0ref DIR` flag: verify with reference f0 (nsff0_raw.bin +
  f0_coarse.bin raw dumps).
- Remaining: RMVPE/FCPE C11 port → replace YIN as default f0.

## How to reproduce the verification

```sh
# fixed CLI vs gold standard
build/wubu_rvc_cli_fixed.exe out/speech/_rvc_in/cartman_base.wav \
  models/rvc/cartman outputs/fixed.wav --model models/rvc/cartman/EricCartmanV1_e650_s10400.pth \
  --f0ref outputs/rvc_ref
# compare outputs/fixed.wav vs outputs/rvc_ref/ref_pytorch.wav → corr 0.9999
```

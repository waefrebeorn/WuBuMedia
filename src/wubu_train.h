#ifndef WUBU_TRAIN_H
#define WUBU_TRAIN_H

/* wubu_train.h — WuBuRVC training pipeline (C11, OpenMP).
 *
 * Phase 5: Training from scratch + transfer learning.
 * Includes AdamW optimizer, loss functions, and training step.
 *
 * License: WaefreBeorn-UMV3
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "wubu_rvc_parity.h"  /* for WuBuRVCModel */

/* ── AdamW optimizer ──
 * Decoupled weight decay Adam (Loshchilov & Hutter 2019).
 * One WuBuAdamW per training job; init each param tensor separately. */
typedef struct WuBuAdamW WuBuAdamW;

WuBuAdamW *wubu_adamw_create(int n_params, float lr, float beta1, float beta2,
                              float eps, float weight_decay);
int wubu_adamw_init_param(WuBuAdamW *opt, int idx, int n_elem);
void wubu_adamw_step(WuBuAdamW *opt, int idx, float *param, const float *grad);
void wubu_adamw_free(WuBuAdamW *opt);

/* ── Loss functions ── */
float wubu_mse_loss(const float *a, const float *b, int n);
float wubu_mae_loss(const float *a, const float *b, int n);
float wubu_stft_loss(const float *a, const float *b, int n, int sr);
float wubu_gan_g_loss(float d_real_out);
float wubu_gan_d_loss(float d_real_out, float d_fake_out);

/* ── Training ──
 * model: loaded WuBuRVCModel with weights
 * opt:   AdamW optimizer (initialized with model's param tensors)
 * mel:   ground-truth mel [mel_frames * mel_channels], [mel_channels, mel_frames] col-major
 * wav:   ground-truth audio [n_samples]
 * loss_out: receives total loss value (optional)
 * Returns 1 = converged, 0 = continue, -1 = error.
 *
 * Note: Full gradient backprop through all layers is a large task.
 * The current implementation computes loss values and the AdamW optimizer
 * is ready for gradient application. The backward pass for each kernel
 * (conv1d, ConvTranspose1d, attention, FFN, flow coupling) will be
 * implemented module-by-module. */
int wubu_train_step(WuBuRVCModel *model, WuBuAdamW *opt,
                    const float *mel, int mel_frames,
                    const float *wav, int n_samples,
                    float *loss_out, int epoch);

/* ── Initialize model weights for training from scratch (RVC v2 defaults) ── */
int wubu_init_weights_rvc2(WuBuRVCModel *model);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_TRAIN_H */

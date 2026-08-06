/* wubu_wubu.h — WuBu model format (.wubu) loader + training pipeline.
 *
 * The .wubu format is our improved container that replaces .pth:
 *   - Same ZIP-based structure as PyTorch .pth (backward compatible)
 *   - Adds: pre-extracted HuBERT/ContentVec features (no runtime encoder)
 *   - Adds: Mind-Meld fused weights (ContentVec+HuBERT+WavLM layer fusion)
 *   - Adds: Monolithic kernel metadata (grid/block dims for CUDA)
 *   - Adds: Training provenance (dataset hash, hyperparameters, epochs)
 *   - Adds: Vocal extraction metadata (Demucs/MDX separation params)
 *
 * The magic: when you load an RVCv2 .pth, we auto-upgrade it to .wubu
 * format — extracting features once, storing them, and using Mind-Meld
 * for better quality — but the output sounds "magically better" because
 * we apply WavLM noise-aware enhancement + ContentVec speaker disentanglement.
 *
 * Research:
 * - RVCv2 .pth contains: state_dict with G (generator), D (discriminator),
 *   posterior encoder, flow, text_encoder, etc.
 * - Pre-trained models: G_200k, G_250k (v2), G_100k, G_300k (v1), etc.
 * - Best training improvements: BigVGAN vocoder, multi-scale mel loss,
 *   cuDNN benchmark, per_preprocess=3.0s for 26% more training chunks
 * - Vocal extraction: UVR5 Demucs, MDX-Net, python-audio-separator
 *
 * License: WaefreBeorn-UMV3
 */

#ifndef WUBU_WUBU_H
#define WUBU_WUBU_H

#include "wubu_rvc_parity.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WUBU_MAGIC    "WUBU"
#define WUBU_VERSION  1

/* ── .wubu format ──
 * ZIP entries:
 *   "weights/"    — tensor data (same format as .pth)
 *   "features/"   — pre-extracted content features (.npy)
 *   "meta.json"   — training metadata (json)
 *   "kernel/"     — monolithic kernel config
 *   "vocab/"      — speaker embeddings (optional) */

/* Load a .wubu model (or auto-upgrade from .pth) */
WuBuRVCModel *wubu_load_model(const char *path);

/* Upgrade a .pth model to .wubu format (auto-extracts features, fuses weights) */
int wubu_upgrade_pth_to_wubu(const char *pth_path, const char *wubu_path);

/* Save model as .wubu (includes training metadata) */
int wubu_save_model(const WuBuRVCModel *model, const char *path,
                    const WuBuTrainingMeta *meta);

/* Check if file is .wubu or .pth, load accordingly */
WuBuRVCModel *wubu_load_auto(const char *path);

/* ── Training pipeline ── */

/* Step 1: Vocal extraction — separates vocals from accompaniment.
 * Uses UVR5/Demucs MDX-Net (our own C11 port, no Python).
 * Input: mixed audio (44100 Hz), Output: isolated vocals (22050 Hz). */
int wubu_vocal_extract(const float *mixed, int n_samples, int sr_in,
                        float *vocals, float *instrumental, int sr_out,
                        const char *separator);

/* Step 2: Dataset preparation — chunks, resamples, extracts features.
 * Splits audio into 3.0s chunks (Applio parity) with 25% overlap. */
int wubu_dataset_prepare(const char *audio_path, const char *out_dir,
                          int target_sr, float chunk_len_s, float overlap);

/* Step 3: Content extraction with Mind-Meld fusion.
 * Runs HuBERT+ContentVec+WavLM simultaneously. */
int wubu_extract_content_fused(const WuBuHuBERT *hubert,
                                const float *pcm, int n_samples,
                                float *feats_out, int max_feats);

/* Step 4: Fine-tuning training step (CPU-only, for when no GPU).
 * Uses AdamW optimizer, multi-scale mel loss. */
int wubu_train_step(WuBuRVCModel *model,
                     const float *mel, int n_frames,
                     const float *wav, int n_samples,
                     float learning_rate, int epoch);

/* Save training config for reproducibility */
int wubu_save_train_config(const WuBuTrainingMeta *meta, const char *path);

/* Load training config */
int wubu_load_train_config(WuBuTrainingMeta *meta, const char *path);

/* Get model info (for display in GUI) */
const WuBuTrainingMeta *wubu_model_meta(const WuBuRVCModel *model);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_WUBU_H */

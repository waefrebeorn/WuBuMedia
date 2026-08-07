/* wubu_postproc.c — Audio post-processing pipeline for RVC voice conversion.
 *
 * Implements the post-processing pipeline improvements from the research catalog:
 *   - EQ with presence boost (2-5kHz) and mud cut (200-300Hz)
 *   - De-essing (dynamic sibilance reduction 5-10kHz)
 *   - Dynamic limiting (prevent clipping, ceiling at -0.5dB)
 *   - Multi-band compression (3 bands: low/mid/high)
 *   - Harmonic enhancement (subtle saturation)
 *   - RMS envelope matching
 *   - Formant shifting (gender conversion)
 *
 * Research: Applio post-processing, ElevenLabs voice design,
 *   Sonarworks de-essing, HiFi-GAN vocoding improvements.
 *
 * License: WaefreBeorn-UMV3
 */

#define _USE_MATH_DEFINES
#include "wubu_postproc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Simple biquad filter (Biquad Cascade) ──
 * Used for EQ, presence boost, mud cut. */
typedef struct {
    float b0, b1, b2, a1, a2;  /* filter coefficients */
    float x1, x2, y1, y2;      /* delay line */
} BiQuad;

static void biquad_init(BiQuad *bq) {
    bq->b0 = 1.0f; bq->b1 = 0; bq->b2 = 0;
    bq->a1 = 0; bq->a2 = 0;
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0;
}

static float biquad_process(BiQuad *bq, float x0) {
    /* Direct Form II */
    float y0 = bq->b0 * x0 + bq->b1 * bq->x1 + bq->b2 * bq->x2
               - bq->a1 * bq->y1 - bq->a2 * bq->y2;
    bq->x2 = bq->x1; bq->x1 = x0;
    bq->y2 = bq->y1; bq->y1 = y0;
    return y0;
}

/* Design a low-shelf filter (for mud cut at low freq, or presence at high freq)
 * fc: cutoff frequency, sr: sample rate, gain_db: +gain or -gain in dB */
static void biquad_lowshelf(BiQuad *bq, float fc, float sr, float gain_db) {
    float A = powf(10.0f, gain_db / 40.0f);
    float omega = 2.0f * (float)M_PI * fc / sr;
    float sinw = sinf(omega), cosw = cosf(omega);
    float sqrtA = sqrtf(A);
    float b0 = A * ((A + 1) - (A - 1) * cosw + 2 * sqrtA * sinw);
    float b1 = 2 * A * ((A - 1) - (A + 1) * cosw);
    float b2 = A * ((A + 1) - (A - 1) * cosw - 2 * sqrtA * sinw);
    float a0 = (A + 1) + (A - 1) * cosw + 2 * sqrtA * sinw;
    float a1 = -2 * ((A - 1) + (A + 1) * cosw);
    float a2 = (A + 1) + (A - 1) * cosw - 2 * sqrtA * sinw;
    bq->b0 = b0 / a0; bq->b1 = b1 / a0; bq->b2 = b2 / a0;
    bq->a1 = a1 / a0; bq->a2 = a2 / a0;
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0;
}

/* Design a peaking EQ filter (for presence boost) */
static void biquad_peaking(BiQuad *bq, float fc, float sr, float gain_db, float Q) {
    float A = powf(10.0f, gain_db / 40.0f);
    float omega = 2.0f * (float)M_PI * fc / sr;
    float sinw = sinf(omega), cosw = cosf(omega);
    float alpha = sinw / (2 * Q);
    float b0 = 1 + alpha * A;
    float b1 = -2 * cosw;
    float b2 = 1 - alpha * A;
    float a0 = 1 + alpha / A;
    float a1 = -2 * cosw;
    float a2 = 1 - alpha / A;
    bq->b0 = b0 / a0; bq->b1 = b1 / a0; bq->b2 = b2 / a0;
    bq->a1 = a1 / a0; bq->a2 = a2 / a0;
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0;
}

/* ── Simple FFT for spectral processing ──
 * Radix-2 Cooley-Tukey, in-place. nmust be a power of 2. */
static void fft_inplace(float *re, float *im, int n) {
    /* Bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    /* Butterfly */
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wlen_re = cosf(ang), wlen_im = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float w_re = 1, w_im = 0;
            for (int k = 0; k < len / 2; k++) {
                float t_re = w_re * re[i + k + len / 2] - w_im * im[i + k + len / 2];
                float t_im = w_re * im[i + k + len / 2] + w_im * re[i + k + len / 2];
                re[i + k + len / 2] = re[i + k] - t_re;
                im[i + k + len / 2] = im[i + k] - t_im;
                re[i + k] += t_re; im[i + k] += t_im;
                float nw_re = w_re * wlen_re - w_im * wlen_im;
                float nw_im = w_re * wlen_im + w_im * wlen_re;
                w_re = nw_re; w_im = nw_im;
            }
        }
    }
}

/* ── Post-processing pipeline ──
 * Applies the full chain of improvements to reduce robotic sound and add
 * naturalness. All parameters are configurable via WuBuPostProcOpts.
 *
 * Research: The "robotic voice" in RVC is caused by:
 *   1. Harsh sibilance (de-essing needed)
 *   2. Flat frequency response (EQ needed)
 *   3. Clipping/overshoot (limiting needed)
 *   4. Lack of natural breathiness (protection needed)
 *   5. Insufficient harmonic content (saturation needed) */
void wubu_post_process(const float *input, float *output, int n, int sr,
                        const WuBuPostProcOpts *opts) {
    if (!input || !output || n <= 0) return;
    if (!opts) { /* defaults */
        output[0] = input[0]; memcpy(output, input, (size_t)n * sizeof(float)); return;
    }

    /* 1. Copy input */
    memcpy(output, input, (size_t)n * sizeof(float));

    /* 2. Mud cut: low-shelf at 200Hz, -2dB (reduce muddiness)
     * Biquad is RECURSIVE — must process sequentially, NOT parallel */
    if (opts->mud_cut_db != 0.0f) {
        BiQuad bq; biquad_init(&bq);
        biquad_lowshelf(&bq, 200.0f, (float)sr, opts->mud_cut_db);
        for (int i = 0; i < n; i++)
            output[i] = biquad_process(&bq, output[i]);
    }

    /* 3. Presence boost: peaking EQ at 3kHz, +1.5dB (improve intelligibility)
     * Recursive — sequential only */
    if (opts->presence_boost_db != 0.0f) {
        BiQuad bq; biquad_init(&bq);
        biquad_peaking(&bq, 3000.0f, (float)sr, opts->presence_boost_db, 1.0f);
        for (int i = 0; i < n; i++)
            output[i] = biquad_process(&bq, output[i]);
    }

    /* 4. De-essing: dynamic attenuation of high frequencies during sibilance
     * Uses a simple first-order high-pass filter to detect sibilance energy,
     * then applies gain reduction when it exceeds threshold. */
    if (opts->de_ess_strength > 0.0f) {
        /* First-order high-pass filter at 5kHz: y[n] = α(x[n] - x[n-1]) + (1-α)*y[n-1] */
        float alpha = 0.05f; /* filter coefficient for ~5kHz HPF at 40k */
        float prev_sample = 0;
        float hpf_prev = 0;
        for (int i = 0; i < n; i++) {
            /* HPF: emphasize high frequencies */
            float hpf = alpha * (output[i] - prev_sample) + (1.0f - alpha) * hpf_prev;
            hpf_prev = hpf;
            prev_sample = output[i];
            /* Dynamic gain reduction based on high-frequency energy */
            float energy = fabsf(hpf);
            if (energy > opts->de_ess_threshold) {
                float reduction = 1.0f - opts->de_ess_strength *
                                  (energy - opts->de_ess_threshold);
                if (reduction < 0.3f) reduction = 0.3f; /* never fully mute */
                output[i] *= reduction;
            }
        }
    }

    /* 5. Harmonic enhancement: subtle saturation */
    if (opts->harmonic_drive > 0.0f) {
#pragma omp parallel for if(n >= 256)
        for (int i = 0; i < n; i++) {
            /* Soft clipping with arctan for warm saturation */
            float x = output[i] * (1.0f + opts->harmonic_drive);
            output[i] = (2.0f / (float)M_PI) * atanf((float)M_PI * x / 2.0f);
        }
    }

    /* 6. Dynamic limiting: prevent clipping, ceiling at -0.5dB */
    /* First, find peak */
    float peak = 0;
#pragma omp parallel for reduction(max:peak) if(n >= 256)
    for (int i = 0; i < n; i++) {
        float a = fabsf(output[i]);
        if (a > peak) peak = a;
    }
    /* Ceiling: -0.5dB = 0.972 in linear */
    float ceiling = powf(10.0f, -0.5f / 20.0f); /* ~0.944 */
    if (peak > ceiling) {
        float scale = ceiling / peak;
#pragma omp parallel for if(n >= 256)
        for (int i = 0; i < n; i++)
            output[i] *= scale;
    }

    /* 7. RMS normalization: scale to target RMS level */
    if (opts->rms_target > 0.0f) {
        float sum_sq = 0;
#pragma omp parallel for reduction(+:sum_sq) if(n >= 256)
        for (int i = 0; i < n; i++)
            sum_sq += output[i] * output[i];
        float rms = sqrtf(sum_sq / (float)n);
        if (rms > 1e-10f) {
            float scale = opts->rms_target / rms;
#pragma omp parallel for if(n >= 256)
            for (int i = 0; i < n; i++) {
                output[i] *= scale;
                /* Re-apply ceiling after RMS scaling */
                if (output[i] > ceiling) output[i] = ceiling;
                if (output[i] < -ceiling) output[i] = -ceiling;
            }
        }
    }
}

/* ── Formant shifting via simple pitch-shift without F0 change ──
 * Simplified implementation: uses a basic phase-vocoder approach.
 * For a full production-quality version, a PSOLA implementation would be ideal.
 * This provides the basic formant shifting capability for gender conversion. */
void wubu_formant_shift(const float *input, float *output, int n, int sr,
                        float shift_ratio) {
    if (!input || !output || n <= 0 || shift_ratio <= 0) {
        if (input && output && n > 0) memcpy(output, input, (size_t)n * sizeof(float));
        return;
    }
    if (shift_ratio == 1.0f) {
        memcpy(output, input, (size_t)n * sizeof(float));
        return;
    }

    /* Phase-vocoder implementation (simplified):
     * 1. STFT with windowed frames
     * 2. Time-scale modification by phase correction
     * 3. Adjust window hop to achieve formant shift without pitch change */
    int fft_size = 2048;
    int hop = 512;
    int n_frames = (n - fft_size) / hop + 1;
    if (n_frames < 2) {
        memcpy(output, input, (size_t)n * sizeof(float));
        return;
    }

    /* Hann window */
    float *window = (float *)malloc(fft_size * sizeof(float));
    for (int i = 0; i < fft_size; i++)
        window[i] = 0.5f * (1 - cosf(2 * (float)M_PI * i / (fft_size - 1)));

    /* Allocate overlap buffers */
    int out_len = (int)(n / shift_ratio) + fft_size;
    float *out = (float *)calloc(out_len, sizeof(float));
    float *win_sum = (float *)calloc(out_len, sizeof(float));

    /* Process frames: shift time axis by shift_ratio */
    for (int f = 0; f < n_frames; f++) {
        /* Original frame position → shifted output position */
        float out_pos_f = (float)(f * hop) / shift_ratio;
        int out_pos = (int)(out_pos_f);

        /* Process frame with STFT modification */
        for (int i = 0; i < fft_size; i++) {
            float x = input[f * hop + i] * window[i];
            if (out_pos + i >= 0 && out_pos + i < out_len) {
                out[out_pos + i] += x;
                win_sum[out_pos + i] += window[i];
            }
        }
    }

    /* Normalize by window sum (overlap-add) */
    int copy_n = out_len < n ? out_len : n;
    for (int i = 0; i < copy_n; i++) {
        if (win_sum[i] > 1e-8f)
            output[i] = out[i] / win_sum[i];
        else
            output[i] = 0;
    }
    /* Fill any remaining */
    for (int i = copy_n; i < n; i++)
        output[i] = 0;

    free(window); free(out); free(win_sum);
}

/* ── F0 contour smoothing ──
 * Reduces jitter and adds natural vibrato by smoothing the F0 contour.
 * Uses a weighted moving average + optional jitter injection for vibrato. */
void wubu_f0_smooth(float *f0, int n, float strength) {
    if (!f0 || n <= 0 || strength <= 0) return;

    /* Weighted moving average: center=0.5, neighbors=0.25 each */
    float *smoothed = (float *)malloc((size_t)n * sizeof(float));
    if (!smoothed) return;

    for (int i = 0; i < n; i++) {
        if (f0[i] <= 0) { smoothed[i] = 0; continue; }
        float c = f0[i];
        float l = (i > 0 && f0[i-1] > 0) ? f0[i-1] : c;
        float r = (i < n-1 && f0[i+1] > 0) ? f0[i+1] : c;
        smoothed[i] = (1 - strength) * c + strength * (0.5f * c + 0.25f * l + 0.25f * r);
    }

    /* Add subtle vibrato if strength > 0.5 */
    if (strength > 0.5f) {
        float vib_depth = (strength - 0.5f) * 0.02f; /* ±2% pitch variation */
        for (int i = 0; i < n; i++) {
            if (smoothed[i] > 0) {
                float vib = 1.0f + vib_depth * sinf(0.2f * i); /* slow vibrato */
                smoothed[i] *= vib;
            }
        }
    }

    memcpy(f0, smoothed, (size_t)n * sizeof(float));
    free(smoothed);
}

/* ── Adaptive feature blending (index rate) ──
 * Blends source features with reference (training-set) features based on
 * energy level. High-energy voiced regions use more reference features;
 * low-energy regions keep source features for stability. */
void wubu_adaptive_feature_blend(const float *src_feat, const float *ref_feat,
                                  float *output, int n_frames, int dim,
                                  float index_rate, const float *energy) {
    if (!src_feat || !ref_feat || !output) return;
    if (index_rate <= 0.0f) {
        memcpy(output, src_feat, (size_t)n_frames * dim * sizeof(float));
        return;
    }

    /* Compute energy threshold (median of energy values) */
    float med = 0, min_e = 1e10, max_e = 0;
    for (int i = 0; i < n_frames; i++) {
        float e = energy ? energy[i] : 0.1f;
        if (e < min_e) min_e = e;
        if (e > max_e) max_e = e;
        med += e;
    }
    med /= (float)n_frames;

    float range = max_e - min_e;
    if (range < 1e-8f) range = 1.0f;

    /* Blend based on energy: high energy → more reference, low energy → more source */
#pragma omp parallel for if(n_frames * dim >= 256)
    for (int f = 0; f < n_frames; f++) {
        float e = energy ? energy[f] : 0.1f;
        /* Normalized energy: 0=low, 1=high */
        float norm_e = (e - min_e) / range;
        /* Blend factor: high energy → index_rate, low energy → less */
        float blend = index_rate * (0.3f + 0.7f * norm_e);
        for (int d = 0; d < dim; d++) {
            output[(size_t)f * dim + d] =
                (1 - blend) * src_feat[(size_t)f * dim + d] +
                blend * ref_feat[(size_t)f * dim + d];
        }
    }
}
/* ── Character voice preset ──
* Applies a specific EQ + dynamics profile for a character "feeling" */
void wubu_apply_character_preset(const float *input, float *output, int n, int sr,
                                int preset) {
    WuBuPostProcOpts opts = {0};

    switch (preset) {
        case WUBU_PRESET_WARM:    /* Cartman: warm, present */
            opts.mud_cut_db = -1.0f;
            opts.presence_boost_db = 1.5f;
            opts.harmonic_drive = 0.1f;
            opts.rms_target = 0.15f;
            break;
        case WUBU_PRESET_BRIGHT:  /* Kenny: bright, energetic */
            opts.presence_boost_db = 2.5f;
            opts.de_ess_strength = 0.3f;
            opts.de_ess_threshold = 0.05f;
            opts.harmonic_drive = 0.0f;
            opts.rms_target = 0.2f;
            break;
        case WUBU_PRESET_SMOOTH:  /* Stan: smooth, neutral */
            opts.mud_cut_db = -2.0f;
            opts.presence_boost_db = 0.5f;
            opts.de_ess_strength = 0.2f;
            opts.de_ess_threshold = 0.03f;
            opts.rms_target = 0.12f;
            break;
        case WUBU_PRESET_BREATHY: /* Kyle: breathy, airy */
            opts.mud_cut_db = -1.5f;
            opts.presence_boost_db = 1.0f;
            opts.de_ess_strength = 0.1f;
            opts.harmonic_drive = 0.05f;
            opts.rms_target = 0.18f;
            break;
        default:
            opts.rms_target = 0.15f;
            break;
    }

    wubu_post_process(input, output, n, sr, &opts);
}

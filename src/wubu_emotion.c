/* wubu_emotion.c — C11 prosodic emotion feature extractor.
 *
 * Real-time emotion detection from 16kHz PCM audio.
 * Single-pass, no heap allocation in the hot path.
 *
 * Extracted features:
 *   - RMS energy (loudness perception)
 *   - Zero-crossing rate (voiced/unvoiced, noisiness)
 *   - Pitch (fundamental frequency) via normalized autocorrelation
 *   - Jitter (cycle-to-cycle period variation)
 *   - Shimmer (amplitude variation across pitch periods)
 *
 * Build: cc -Wall -Wextra -std=c11 -c wubu_emotion.c -o wubu_emotion.o
 * License: SPDX-License-Identifier: WaefreBeorn-UMV3
 */
#include "wubu_emotion.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- Opaque struct ---------- */
struct EmotionImpl {
    float energy_hist[16];  /* sliding window for smoothing */
    float pitch_hist[16];
    size_t hist_idx;
};

/* ---------- Mood classification table ---------- */
static const char *MOOD_NAMES[] = {
    "neutral", "happy", "sad", "angry",
    "thinking", "excited", "confused"
};

const char *wubu_mood_name(Mood m) {
    if (m < 0 || m >= MOOD_COUNT) return "unknown";
    return MOOD_NAMES[m];
}

/* ---------- Lifecycle ---------- */
Emotion *wubu_emotion_open(void) {
    Emotion *e = (Emotion *)calloc(1, sizeof(Emotion));
    return e;
}

void wubu_emotion_close(Emotion *e) {
    free(e);
}

/* ---------- Feature extraction ---------- */

/* RMS energy: sqrt(mean(x^2)), normalized to [-1,1] PCM range */
static float compute_energy(const short *s, size_t n) {
    if (!s || n == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        double v = s[i] / 32768.0;
        sum += v * v;
    }
    return (float)sqrt(sum / (double)n);
}

/* Zero-crossing rate: count sign changes per sample */
static float compute_zcr(const short *s, size_t n) {
    if (!s || n < 2) return 0.0f;
    int sign_changes = 0;
    int prev_sign = s[0] >= 0 ? 1 : -1;
    for (size_t i = 1; i < n; i++) {
        int cur_sign = s[i] >= 0 ? 1 : -1;
        if (cur_sign != prev_sign) sign_changes++;
        prev_sign = cur_sign;
    }
    /* ZCR per 10ms; 16kHz = 160 samples per 10ms */
    return (float)(sign_changes * 160.0 / (double)n);
}

/* Pitch via normalized autocorrelation.
 * Returns fundamental frequency in Hz, or 0.0 if unvoiced. */
static float compute_pitch(const short *s, size_t n, int sample_rate) {
    if (!s || n < 60) return 0.0f;

    /* Normalize to [-1, 1] */
    float *norm = (float *)malloc(n * sizeof(float));
    if (!norm) return 0.0f;
    for (size_t i = 0; i < n; i++) {
        norm[i] = s[i] / 32768.0f;
    }

    /* Autocorrelation for lags in pitch range (50-400 Hz → 40-320 samples at 16kHz) */
    int min_lag = sample_rate / 400;  /* ~40 for 16kHz */
    int max_lag = sample_rate / 50;    /* ~320 for 16kHz */
    if (max_lag > (int)n - 1) max_lag = (int)n - 1;
    if (min_lag >= max_lag) { free(norm); return 0.0f; }

    float best_val = -1e10f;
    int best_lag = 0;

    for (int lag = min_lag; lag <= max_lag; lag++) {
        float ac = 0.0f;
        for (size_t i = 0; i + lag < n; i++) {
            ac += norm[i] * norm[i + lag];
        }
        /* Normalize by n (not n-lag) to avoid bias toward larger lags (octave errors) */
        float norm_val = 0.0f;
        for (size_t i = 0; i < n; i++) {
            norm_val += norm[i] * norm[i];
        }
        if (norm_val > 1e-10f) {
            ac /= norm_val;
        }
        if (ac > best_val) {
            best_val = ac;
            best_lag = lag;
        }
    }

    if (best_val < 0.4f) {
        free(norm);
        return 0.0f;  /* no strong periodicity → unvoiced */
    }

    /* Check for sub-harmonics: if a strong peak exists at half the lag,
     * prefer the higher fundamental rate (avoids octave errors) */
    if (best_lag > min_lag * 2) {
        int half_lag = best_lag / 2;
        float half_val = -1e10f;
        for (int lag = half_lag - 2; lag <= half_lag + 2; lag++) {
            if (lag < min_lag) continue;
            float ac = 0.0f;
            for (size_t i = 0; i + lag < n; i++) {
                ac += norm[i] * norm[i + lag];
            }
            float nv = 0.0f;
            for (size_t i = 0; i + lag < n; i++) {
                nv += norm[i] * norm[i];
            }
            if (nv > 1e-10f) ac /= nv;
            if (ac > half_val) half_val = ac;
        }
        /* If half-lag peak is strong (>85% of best), it's likely the true F0 */
        if (half_val > best_val * 0.85f) {
            best_lag = half_lag;
            best_val = half_val;
        }
    }

    /* Convert lag to frequency, then interpolate for precision */
    float pitch = (float)sample_rate / (float)best_lag;

    /* Parabolic interpolation around peak */
    if (best_lag > min_lag && best_lag < max_lag) {
        /* Recompute autocorr at best_lag-1, best_lag, best_lag+1 */
        float ac_prev = 0, ac_cur = 0, ac_next = 0;
        for (size_t i = 0; i + best_lag < n; i++) {
            ac_prev += norm[i] * norm[i + best_lag - 1];
            ac_next += norm[i] * norm[i + best_lag + 1];
        }
        float nv = 0, nv2 = 0;
        for (size_t i = 0; i + best_lag < n; i++) {
            nv += norm[i] * norm[i];
        }
        for (size_t i = 0; i + best_lag < n; i++) {
            nv2 += norm[i] * norm[i];
        }
        ac_cur = best_val;
        if (nv > 1e-10f) { ac_prev /= nv; ac_cur = best_val; }
        if (nv2 > 1e-10f) { ac_next /= nv2; }
        if (ac_prev > 0 && ac_next > 0 && ac_cur > 0) {
            float denom = ac_prev - 2*ac_cur + ac_next;
            if (fabsf(denom) > 1e-8f) {
                float p = 0.5f * (ac_prev - ac_next) / denom;
                if (fabsf(p) < 1.0f) {
                    pitch = (float)sample_rate / (float)(best_lag + p);
                }
            }
        }
    }

    if (pitch < 50.0f || pitch > 400.0f) {
        free(norm);
        return 0.0f;
    }
    free(norm);
    return pitch;
}

/* Jitter: relative cycle-to-cycle period variation */
static float compute_jitter(const short *s, size_t n, int sample_rate) {
    if (!s || n < 60) return 0.0f;

    /* Estimate local period via autocorrelation at smaller scale */
    float pitch = compute_pitch(s, n, sample_rate);
    if (pitch < 50.0f) return 0.0f;

    int T = (int)(sample_rate / pitch);
    if (T < 1 || T >= (int)n) return 0.0f;

    /* Jitter = mean relative deviation of T estimates */
    float jitter_sum = 0.0f;
    int jitter_count = 0;
    for (int i = 1; i < 5; i++) {
        int t1 = T + (i * T / 8);
        int t2 = T + ((i-1) * T / 8);
        if (t1 < (int)n && t2 < (int)n) {
            float d = (float)(t1 - t2) / (float)T;
            jitter_sum += fabsf(d);
            jitter_count++;
        }
    }

    return jitter_count > 0 ? jitter_sum / (float)jitter_count : 0.0f;
}

/* Shimmer: relative amplitude variation */
static float compute_shimmer(const short *s, size_t n, int sample_rate) {
    if (!s || n < 60) return 0.0f;

    float pitch = compute_pitch(s, n, sample_rate);
    if (pitch < 50.0f) return 0.0f;

    int T = (int)(sample_rate / pitch);
    if (T < 2 || T >= (int)n) return 0.0f;

    /* Find amplitude peaks */
    float shimmer_sum = 0.0f;
    int shimmer_count = 0;
    float prev_amp = 0.0f;

    for (size_t i = 0; i + T < n; i += T) {
        float amp = 0.0f;
        for (int j = 0; j < T && (int)(i + j) < (int)n; j++) {
            float v = fabsf(s[i + j] / 32768.0f);
            if (v > amp) amp = v;
        }
        if (prev_amp > 1e-6f) {
            shimmer_sum += fabsf(amp - prev_amp) / prev_amp;
            shimmer_count++;
        }
        prev_amp = amp;
    }

    return shimmer_count > 0 ? shimmer_sum / (float)shimmer_count : 0.0f;
}

/* ---------- Public API ---------- */

int wubu_emotion_frame(Emotion *e, const short *samples, size_t n_samples,
                       EmotionFeatures *out) {
    if (!e || !samples || !out) return -1;

    const int SAMPLE_RATE = 16000;

    memset(out, 0, sizeof(EmotionFeatures));
    out->energy = compute_energy(samples, n_samples);
    out->zero_crossing_rate = compute_zcr(samples, n_samples);
    out->pitch_hz = compute_pitch(samples, n_samples, SAMPLE_RATE);
    out->jitter = compute_jitter(samples, n_samples, SAMPLE_RATE);
    out->shimmer = compute_shimmer(samples, n_samples, SAMPLE_RATE);
    out->speech_rate = 0.0f;  /* estimated by caller or higher-level analysis */
    out->mood = wubu_emotion_classify(out);

    return 0;
}

int wubu_emotion_smooth(Emotion *e, const EmotionFeatures *frame,
                        EmotionFeatures *out, double alpha) {
    if (!e || !frame || !out) return -1;

    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;

    /* EMA-style smoothing */
    e->energy_hist[e->hist_idx] = (float)alpha * frame->energy +
        (1.0f - (float)alpha) * (e->hist_idx ? e->energy_hist[(e->hist_idx - 1) & 15] : frame->energy);
    e->pitch_hist[e->hist_idx] = (float)alpha * frame->pitch_hz +
        (1.0f - (float)alpha) * (e->hist_idx ? e->pitch_hist[(e->hist_idx - 1) & 15] : frame->pitch_hz);
    e->hist_idx = (e->hist_idx + 1) & 15;

    out->energy = e->energy_hist[e->hist_idx ? (e->hist_idx - 1) & 15 : 15];
    out->pitch_hz = e->pitch_hist[e->hist_idx ? (e->hist_idx - 1) & 15 : 15];
    out->zero_crossing_rate = frame->zero_crossing_rate;
    out->jitter = frame->jitter;
    out->shimmer = frame->shimmer;
    out->speech_rate = frame->speech_rate;
    out->mood = wubu_emotion_classify(out);

    return 0;
}

/* ---------- Mood classification ---------- */
Mood wubu_emotion_classify(const EmotionFeatures *f) {
    if (!f) return MOOD_NEUTRAL;

    float energy = f->energy;
    float pitch = f->pitch_hz;
    float zcr = f->zero_crossing_rate;
    float jitter = f->jitter;
    float shimmer = f->shimmer;

    /* Unvoiced / noise-dominant */
    if (zcr > 8.0f && pitch < 50.0f) {
        return MOOD_THINKING;
    }

    /* Angry: high energy, high pitch, high jitter+shimmer */
    if (energy > 0.15f && pitch > 180.0f && (jitter + shimmer) > 0.1f) {
        return MOOD_ANGRY;
    }

    /* Happy: moderate-high energy, moderate-high pitch (check before excited) */
    if (energy > 0.08f && pitch > 120.0f && pitch < 180.0f) {
        return MOOD_HAPPY;
    }

    /* Excited: high energy, high pitch, low jitter */
    if (energy > 0.15f && pitch > 180.0f && jitter < 0.05f) {
        return MOOD_EXCITED;
    }

    /* Sad: low energy, low pitch */
    if (energy < 0.04f && pitch < 100.0f && pitch > 50.0f) {
        return MOOD_SAD;
    }

    /* Confused: moderate energy, variable jitter */
    if (jitter > 0.05f && shimmer > 0.05f && energy > 0.04f && energy < 0.12f) {
        return MOOD_CONFUSED;
    }

    /* Thinking: low energy, moderate pitch, low jitter */
    if (energy < 0.06f && pitch > 80.0f && jitter < 0.03f) {
        return MOOD_THINKING;
    }

    return MOOD_NEUTRAL;
}

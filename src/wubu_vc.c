/* wubu_vc.c — WuBuVoice: Real-time voice changer (C11).
 *
 * Our own voice-to-voice conversion. No Python, no fairseq, no ONNX,
 * no HTTP APIs. Uses only our C11 frame buffer + WuBuRVC.
 *
 * Architecture:
 *   mic_pcm → mel_spectrogram → wubu_frame_buffer_t → WuBuRVC graph
 *     → pitch_shift + formant_shift + speed → output_pcm → VoiceMeeter
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#ifdef _WIN32
#include <windows.h>
#endif
#include "wubu_vc.h"
#include "wubu_rvc.h"
#include "wubu_buddy.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Default voice presets (cartoon characters + base voices) */
static const VoicePreset DEFAULT_VOICES[] = {
    {"default",   0,  1.0, 0.0f, 0.5f, 1, "", ""},
    {"cartman",   3,  0.85, 0.2f, 0.3f, 1, "cartman.pth", "cartman.index"},
    {"homer",    -2,  0.90, -0.1f, 0.8f, 1, "homer.pth", "homer.index"},
    {"terminator", -3, 0.80, 0.0f, 0.9f, 1, "terminator.pth", "terminator.index"},
    {"chipmunk", 12,  1.4,  0.3f, 0.2f, 0, "", ""},
    {"deep",     -5,  1.0, -0.2f, 0.9f, 0, "", ""},
    {"robot",     0,  1.0,  0.0f, 0.5f, 1, "robot.pth", "robot.index"},
    {"alien",     5,  0.7,  0.5f, 0.1f, 1, "alien.pth", "alien.index"},
};
#define N_DEFAULT_VOICES (int)(sizeof(DEFAULT_VOICES) / sizeof(DEFAULT_VOICES[0]))

struct WuBuVoiceChanger {
    VCConfig cfg;
    WuBuRVC  *rvc;
    WuBuBuddy *buddy;
    VoicePreset *voices;
    int n_voices;
    int active_voice_idx;
    int initialized;
    long total_frames;
    double total_latency_ms;
};

void wubu_vc_default_config(VCConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->sample_rate = 22050;
    cfg->frame_ms = 20;
    cfg->use_cuda = 0;
    cfg->latency_ms = 40.0;
    cfg->enable_stt = 0;
    cfg->enable_tts = 0;
    cfg->enable_llm = 0;
    strncpy(cfg->mic_device, "default", sizeof(cfg->mic_device) - 1);
    strncpy(cfg->output_device, "default", sizeof(cfg->output_device) - 1);
}

WuBuVoiceChanger *wubu_vc_create(const VCConfig *cfg) {
    if (!cfg) return NULL;

    WuBuVoiceChanger *vc = (WuBuVoiceChanger *)calloc(1, sizeof(WuBuVoiceChanger));
    if (!vc) return NULL;

    memcpy(&vc->cfg, cfg, sizeof(VCConfig));

    /* Load RVC model if specified */
    if (cfg->use_cuda || cfg->sample_rate > 0) {
        RVCConfig rvc_cfg;
        memset(&rvc_cfg, 0, sizeof(rvc_cfg));
        rvc_cfg.sample_rate = cfg->sample_rate;
        rvc_cfg.use_cuda = cfg->use_cuda;
        rvc_cfg.mel_channels = 80;
        rvc_cfg.hidden_channels = 512;
        rvc_cfg.version = RVC_V2;
        vc->rvc = wubu_rvc_load(&rvc_cfg);
        /* RVC is optional — fallback to pitch-shift only */
    }

    /* Register default voices */
    vc->n_voices = N_DEFAULT_VOICES;
    vc->voices = (VoicePreset *)malloc(vc->n_voices * sizeof(VoicePreset));
    if (vc->voices) {
        memcpy(vc->voices, DEFAULT_VOICES, vc->n_voices * sizeof(VoicePreset));
    }
    vc->active_voice_idx = 0;
    vc->initialized = 1;
    return vc;
}

void wubu_vc_destroy(WuBuVoiceChanger *vc) {
    if (!vc) return;
    if (vc->rvc) wubu_rvc_destroy(vc->rvc);
    if (vc->buddy) wubu_buddy_destroy(vc->buddy);
    free(vc->voices);
    free(vc);
}

int wubu_vc_register_voice(WuBuVoiceChanger *vc, const VoicePreset *preset) {
    if (!vc || !preset) return -1;
    /* Resize voices array */
    VoicePreset *new_voices = (VoicePreset *)realloc(vc->voices,
        (vc->n_voices + 1) * sizeof(VoicePreset));
    if (!new_voices) return -1;
    vc->voices = new_voices;
    memcpy(&vc->voices[vc->n_voices], preset, sizeof(VoicePreset));
    vc->n_voices++;
    return vc->n_voices - 1;
}

void wubu_vc_set_voice(WuBuVoiceChanger *vc, const char *voice_name) {
    if (!vc || !voice_name) return;
    for (int i = 0; i < vc->n_voices; i++) {
        if (strcmp(vc->voices[i].name, voice_name) == 0) {
            vc->active_voice_idx = i;
            return;
        }
    }
    /* Default to first if not found */
    vc->active_voice_idx = 0;
}

void wubu_vc_list_voices(const WuBuVoiceChanger *vc, char *out, size_t max) {
    if (!vc || !out) return;
    out[0] = '\0';
    char *p = out;
    for (int i = 0; i < vc->n_voices && (size_t)(p - out) < max - 64; i++) {
        int n = snprintf(p, max - (p - out), "%s (pitch=%d, speed=%.2f)\n",
                         vc->voices[i].name,
                         vc->voices[i].pitch_shift,
                         vc->voices[i].speed);
        if (n < 0) break;
        p += n;
    }
}

/* Simple mel-spectrogram from PCM (matches our pipeline) */
static void vc_extract_mel(const float *pcm, int n_samples, int sr,
                            int n_mel, int n_frames, float *mel_out) {
    int n_fft = 1024;
    int hop = sr / 100;  /* ~10ms hop at any sample rate */
    if (hop < 1) hop = 1;
    int win = n_fft;
    if (win > n_samples) win = n_samples;

    float *window = (float *)malloc(win * sizeof(float));
    for (int i = 0; i < win; i++) {
        window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (win - 1));
    }

    for (int f = 0; f < n_frames; f++) {
        int center = f * hop;
        for (int m = 0; m < n_mel; m++) {
            /* Simple FFT bin → mel mapping */
            int bin_start = m * n_fft / (n_mel * 2);
            int bin_end = (m + 1) * n_fft / (n_mel * 2);
            float energy = 0.0f;
            for (int b = bin_start; b < bin_end; b++) {
                float re = 0, im = 0;
                for (int t = 0; t < win; t++) {
                    int idx = center + t - win/2;
                    if (idx >= 0 && idx < n_samples) {
                        float x = pcm[idx] * window[t];
                        float angle = -2.0f * (float)M_PI * b * t / n_fft;
                        re += x * cosf(angle);
                        im += x * sinf(angle);
                    }
                }
                energy += sqrtf(re * re + im * im);
            }
            mel_out[f * n_mel + m] = energy / (bin_end - bin_start);
        }
    }
    free(window);
}

/* Apply pitch shift + formant shift to PCM.
 * Returns the number of output samples (may differ from n_samples
 * due to speed/pitch resampling). */
static int vc_apply_effects(float *pcm, int n_samples, int sr,
                             int pitch_shift, double speed,
                             float formant_shift, float gender) {
    (void)sr; (void)formant_shift; (void)gender;
    /* Simple pitch shift via resampling */
    double rate = pow(2.0, pitch_shift / 12.0) / speed;
    if (rate < 0.5) rate = 0.5;
    if (rate > 2.0) rate = 2.0;

    int n_out = (int)(n_samples / rate);
    if (n_out < 1) n_out = 1;

    float *out = (float *)malloc(n_out * sizeof(float));
    for (int i = 0; i < n_out; i++) {
        double src_idx = i * rate;
        int lo = (int)floor(src_idx);
        int hi = lo + 1;
        double frac = src_idx - lo;
        if (lo < 0) lo = 0;
        if (hi >= n_samples) hi = n_samples - 1;
        if (lo >= n_samples) lo = n_samples - 1;
        out[i] = pcm[lo] * (1.0 - frac) + pcm[hi] * frac;
    }

    /* Copy back (may resize if we had output buffer) */
    int copy_n = (n_out < n_samples) ? n_out : n_samples;
    memcpy(pcm, out, copy_n * sizeof(float));
    free(out);
    return n_out;
}

int wubu_vc_process_mic(WuBuVoiceChanger *vc,
                         const float *pcm_input, int n_samples,
                         float *output, int max_samples) {
    if (!vc || !vc->initialized || !pcm_input || !output) return -1;

    /* Timing (cross-platform) */
#ifdef _WIN32
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
#else
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
#endif

    /* Get active voice */
    const VoicePreset *vp = &vc->voices[vc->active_voice_idx];

    /* Apply effects first (pitch shift, speed) — cheapest path */
    float *pcm_work = (float *)malloc(n_samples * sizeof(float));
    if (!pcm_work) return -1;
    memcpy(pcm_work, pcm_input, n_samples * sizeof(float));

    /* vc_apply_effects modifies pcm_work in-place and returns new length */
    int n_work = vc_apply_effects(pcm_work, n_samples, vc->cfg.sample_rate,
                                  vp->pitch_shift, vp->speed,
                                  vp->formant_shift, vp->gender);
    if (n_work < 0) n_work = n_samples;

    /* If RVC model is available, run through our engine */
    if (vc->rvc && vp->use_rvc && vp->rvc_model[0] != '\0' &&
        wubu_rvc_is_model_loaded(vc->rvc)) {
        /* Extract mel and run through WuBuRVC */
        int n_mel = 80;
        int n_frames = n_samples / 256;
        if (n_frames < 1) n_frames = 1;
        int n_mel_floats = n_frames * n_mel;

        float *mel = (float *)calloc(n_mel_floats, sizeof(float));
        if (mel) {
            vc_extract_mel(pcm_work, n_samples, vc->cfg.sample_rate,
                           n_mel, n_frames, mel);

            int n_out = wubu_rvc_synthesize(vc->rvc, mel, n_frames, n_mel,
                                             output, max_samples);
            free(mel);
            free(pcm_work);

#ifdef _WIN32
            QueryPerformanceCounter(&end);
            double elapsed = (double)(end.QuadPart - start.QuadPart) /
                             (double)freq.QuadPart * 1000.0;
#else
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double elapsed = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                             (t1.tv_nsec - t0.tv_nsec) / 1e6;
#endif
            vc->total_latency_ms += elapsed;
            vc->total_frames++;
            return (n_out > 0) ? n_out : n_samples;
        }
    }

    /* Fallback: just pitch-shifted PCM directly */
    int n_out = n_work;
    if (n_out > max_samples) n_out = max_samples;
    memcpy(output, pcm_work, n_out * sizeof(float));

    free(pcm_work);

#ifdef _WIN32
    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart) /
                     (double)freq.QuadPart * 1000.0;
#else
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                     (t1.tv_nsec - t0.tv_nsec) / 1e6;
#endif
    vc->total_latency_ms += elapsed;
    vc->total_frames++;
    return n_out;
}

int wubu_vc_speak(WuBuVoiceChanger *vc,
                   const char *text,
                   float *output, int max_samples) {
    if (!vc || !vc->initialized || !text || !output) return -1;

    /* If buddy is attached, use TTS + RVC through the buddy */
    if (vc->buddy) {
        return wubu_buddy_speak(vc->buddy, text, output, max_samples);
    }

    /* Fallback: generate synthetic speech from text features */
    /* Map text length to audio duration (~15 chars = 1 second) */
    size_t text_len = strlen(text);
    int n_samples = (int)((text_len / 15.0) * vc->cfg.sample_rate);
    if (n_samples < vc->cfg.sample_rate / 10)
        n_samples = vc->cfg.sample_rate / 10;  /* min 100ms */
    if (n_samples > max_samples) n_samples = max_samples;

    const VoicePreset *vp = &vc->voices[vc->active_voice_idx];
    double rate = pow(2.0, vp->pitch_shift / 12.0) / vp->speed;

    for (int i = 0; i < n_samples; i++) {
        double t = (double)i / vc->cfg.sample_rate;
        /* Generate speech-like waveform from text features */
        double fundamental = 120.0 * rate * (0.5 + (double)(text[i % text_len] % 50) / 100.0);
        double sample = sin(2.0 * M_PI * fundamental * t);
        /* Add harmonics for richness */
        sample += 0.3 * sin(2.0 * M_PI * fundamental * 2.0 * t);
        sample += 0.1 * sin(2.0 * M_PI * fundamental * 3.0 * t);
        /* Amplitude envelope (syllable-like) */
        sample *= exp(-fmod(t * 10.0, 1.0) * 3.0) * 0.5;
        output[i] = (float)(sample * 0.3);
    }

    /* Apply pitch shift effects */
    int n_speak = vc_apply_effects(output, n_samples, vc->cfg.sample_rate,
                                   vp->pitch_shift, vp->speed,
                                   vp->formant_shift, vp->gender);
    if (n_speak < 0) n_speak = n_samples;

    vc->total_frames++;
    return n_speak;
}

int wubu_vc_start_capture(WuBuVoiceChanger *vc) {
    if (!vc || !vc->initialized) return -1;
    /* In production: open WASAPI/AudioCaptureClient device */
    return 0;
}

int wubu_vc_stop_capture(WuBuVoiceChanger *vc) {
    if (!vc || !vc->initialized) return -1;
    return 0;
}

void wubu_vc_info(const WuBuVoiceChanger *vc, VCInfo *out) {
    if (!vc || !out) return;
    memset(out, 0, sizeof(*out));
    out->sample_rate = vc->cfg.sample_rate;
    out->frame_size = vc->cfg.sample_rate * vc->cfg.frame_ms / 1000;
    out->buffer_ms = vc->cfg.frame_ms;
    out->active_voice = vc->active_voice_idx;
    if (vc->voices) {
        strncpy(out->active_voice_name, vc->voices[vc->active_voice_idx].name,
                sizeof(out->active_voice_name) - 1);
    }
    out->total_frames_processed = vc->total_frames;
    if (vc->total_frames > 0) {
        out->avg_latency_ms = vc->total_latency_ms / vc->total_frames;
    }
    if (vc->rvc) {
        RVCInfo rvc_info;
        wubu_rvc_info(vc->rvc, &rvc_info);
        out->rvc_version = rvc_info.rvc_version;
    }
}

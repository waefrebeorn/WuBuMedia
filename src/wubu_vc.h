#ifndef WUBU_VC_H
#define WUBU_VC_H

/* wubu_vc.h — WuBuVoice: Real-time voice changer (C11).
 *
 * Our own voice-to-voice conversion — no Python, no fairseq, no ONNX,
 * no HTTP APIs. Just C11 + our virtualized frame buffer + WuBuRVC.
 *
 * Architecture (our own design):
 *
 *   mic → PCM ring buffer → feature extraction (mel) → WuBuRVC graph
 *     → frame buffer → fused kernels → output waveform → VoiceMeeter
 *
 * The entire pipeline runs through wubu_frame_buffer_t — a unified
 * CPU/GPU buffer space we design like a game engine. No legacy
 * Python pipeline holds us back.
 *
 * License: WaefreBeorn-UMV3
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WuBuVoiceChanger WuBuVoiceChanger;
typedef struct VCConfig VCConfig;
typedef struct VCInfo VCInfo;

/* Voice preset — our own character definitions */
typedef struct {
    char   name[64];        /* voice name, e.g. "cartman", "terminator" */
    int    pitch_shift;     /* semitones (-12 to +12) */
    double speed;           /* speech rate multiplier (0.5 to 2.0) */
    float  formant_shift;   /* vocal tract formant shift */
    float  gender;          /* 0.0=feminine, 1.0=masculine blend */
    int    use_rvc;         /* 1 = apply RVC conversion, 0 = pitch-shift only */
    char   rvc_model[512];  /* model path (if use_rvc) */
    char   rvc_index[512];  /* index path (if use_rvc) */
} VoicePreset;

/* Configuration */
struct VCConfig {
    char       mic_device[256];
    char       output_device[256];
    int        sample_rate;     /* 22050 or 44100 */
    int        frame_ms;        /* audio frame size in ms (20-50) */
    int        use_cuda;        /* 1 = GPU kernels, 0 = CPU */
    int        gpu_device;      /* CUDA device index */
    double     latency_ms;      /* target latency */
    int        enable_stt;      /* 1 = enable speech-to-text */
    char       stt_model[256];  /* whisper model path */
    int        enable_tts;      /* 1 = enable text-to-speech */
    char       tts_model[512];  /* piper ONNX or our own model */
    int        enable_llm;      /* 1 = enable LLM response */
    char       llm_endpoint[256]; /* NIM or local endpoint */
};

/* Runtime info */
struct VCInfo {
    int    sample_rate;
    int    frame_size;
    int    buffer_ms;
    int    active_voice;
    char   active_voice_name[64];
    int    rvc_version;
    float  current_mel_hz;
    long   total_frames_processed;
    double avg_latency_ms;
    double avg_fps;
};

/* Default config */
void wubu_vc_default_config(VCConfig *cfg);

/* Voice management */
int  wubu_vc_register_voice(WuBuVoiceChanger *vc, const VoicePreset *preset);
void wubu_vc_set_voice(WuBuVoiceChanger *vc, const char *voice_name);
void wubu_vc_list_voices(const WuBuVoiceChanger *vc, char *out, size_t max);

/* Create/destroy voice changer */
WuBuVoiceChanger *wubu_vc_create(const VCConfig *cfg);
void              wubu_vc_destroy(WuBuVoiceChanger *vc);

/* Real-time processing */
int wubu_vc_process_mic(WuBuVoiceChanger *vc,
                         const float *pcm_input, int n_samples,
                         float *output, int max_samples);

/* Text-to-speech: generate voice from text (using our own TTS) */
int wubu_vc_speak(WuBuVoiceChanger *vc,
                   const char *text,
                   float *output, int max_samples);

/* VoiceMeeter integration */
int wubu_vc_start_capture(WuBuVoiceChanger *vc);
int wubu_vc_stop_capture(WuBuVoiceChanger *vc);

/* Info */
void wubu_vc_info(const WuBuVoiceChanger *vc, VCInfo *out);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_VC_H */
#ifndef WUBU_BUDDY_H
#define WUBU_BUDDY_H

/* wubu_buddy.h — Interactive Buddy: emotion engine + TTS + WuBuRVC voice.
 *
 * The buddy uses:
 *   - wubu_rlm: OCEAN personality + mood decay (emotion state)
 *   - wubu_rvc: our custom RVC inference (voice synthesis)
 *   - Piper TTS: text → mel-spectrogram (CPU, fast, low VRAM)
 *
 * The buddy's voice is personality-modulated:
 *   - Happy mood → higher pitch, brighter timbre
 *   - Sad mood → lower pitch, warmer timbre
 *   - Angry mood → more aggressive timbre, faster speech
 *
 * License: WaefreBeorn-UMV3
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WuBuBuddy WuBuBuddy;

typedef struct {
    char  tts_model[256];     /* Piper TTS model path */
    char  rvc_model[512];     /* WuBuRVC .pth path */
    char  rvc_index[512];     /* WuBuRVC .index path */
    int   sample_rate;        /* output sample rate */
    int   use_cuda;           /* RVC on CUDA */
    char  rlm_db[256];        /* RLM SQLite path */
    char  context_slug[128];  /* RLM context */
} BuddyConfig;

typedef struct {
    char  mood_name[32];      /* current mood classification */
    float valence;            /* emotional valence [-1, 1] */
    float arousal;            /* arousal level [0, 1] */
    float openness;
    float conscientiousness;
    float extraversion;
    float agreeableness;
    float neuroticism;
    int   total_spoken;
    int   total_utterances;
    char  last_text[512];     /* last text spoken */
} BuddyState;

/* Create the interactive buddy */
WuBuBuddy *wubu_buddy_create(const BuddyConfig *cfg);

/* Destroy the buddy */
void wubu_buddy_destroy(WuBuBuddy *buddy);

/* Speak text: TTS → RVC → output waveform.
 * Returns number of samples generated, or error code. */
int wubu_buddy_speak(WuBuBuddy *buddy, const char *text,
                      float *output, int max_samples);

/* Get current buddy state (mood + personality + stats) */
void wubu_buddy_state(const WuBuBuddy *buddy, BuddyState *out);

/* Update mood (called after each conversation turn) */
void wubu_buddy_update_mood(WuBuBuddy *buddy, double valence, double arousal);

/* Set personality (OCEAN model) */
void wubu_buddy_set_personality(WuBuBuddy *buddy,
                                 float openness, float conscientiousness,
                                 float extraversion, float agreeableness,
                                 float neuroticism);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_BUDDY_H */

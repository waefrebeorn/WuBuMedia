#ifndef WUBU_EMOTION_H
#define WUBU_EMOTION_H
/* wubu_emotion.h — C11 prosodic emotion feature extractor (real-time).
 *
 * Extracts pitch, energy, jitter, shimmer, zero-crossing rate, and speech rate
 * from 16kHz PCM audio frames. Maps to a mood enum for the avatar system.
 *
 * Opaque struct pattern: EmotionImpl is hidden in the .c file.
 *
 * Build: cc -c wubu_emotion.c -o wubu_emotion.o -Wall -Wextra -std=c11
 * License: SPDX-License-Identifier: WaefreBeorn-UMV3
 */

#include <stddef.h>

/* Mood categories for the avatar */
typedef enum {
    MOOD_NEUTRAL = 0,
    MOOD_HAPPY   = 1,
    MOOD_SAD     = 2,
    MOOD_ANGRY   = 3,
    MOOD_THINKING= 4,
    MOOD_EXCITED = 5,
    MOOD_CONFUSED= 6,
    MOOD_COUNT   = 7
} Mood;

const char *wubu_mood_name(Mood m);

/* Emotion features extracted from a 20ms audio frame (16kHz = 320 samples) */
typedef struct {
    float energy;          /* RMS amplitude, 0.0-1.0 */
    float zero_crossing_rate; /* sign changes per 10ms */
    float pitch_hz;        /* fundamental frequency, 0 if unvoiced */
    float jitter;          /* cycle-to-cycle period variation */
    float shimmer;         /* amplitude variation */
    float speech_rate;     /* syllables/sec estimate */
    Mood  mood;            /* derived mood classification */
} EmotionFeatures;

/* Opaque emotion engine handle */
typedef struct EmotionImpl Emotion;

/* Lifecycle */
Emotion *wubu_emotion_open(void);
void     wubu_emotion_close(Emotion *e);

/* Process one 20ms frame of 16kHz int16 PCM (320 samples recommended).
 * Returns features; mood is derived automatically. */
int wubu_emotion_frame(Emotion *e, const short *samples, size_t n_samples,
                       EmotionFeatures *out);

/* Smooth features over a sliding window for stable mood output.
 * Call wubu_emotion_frame() then wubu_emotion_smooth() for each frame. */
int wubu_emotion_smooth(Emotion *e, const EmotionFeatures *frame,
                        EmotionFeatures *out, double alpha);

/* Convert raw prosodic features to a Mood enum. */
Mood wubu_emotion_classify(const EmotionFeatures *f);

#endif /* WUBU_EMOTION_H */

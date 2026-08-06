/* test_emotion.c — Test harness for wubu_emotion (C11).
 *
 * Tests prosodic feature extraction: energy, ZCR, pitch, jitter, shimmer,
 * and mood classification on synthetic audio frames.
 *
 * Build: cc -Wall -Wextra -std=c11 test_emotion.c wubu_emotion.c -lm -I . -o test_emotion
 */
#include "wubu_emotion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_run = 0;
static int tests_pass = 0;

#define RUN_TEST(name, func) do { \
    tests_run++; \
    printf("[RUN] %s\n", name); \
    fflush(stdout); \
    if (func()) { tests_pass++; printf("[PASS] %s\n", name); } \
    else { printf("[FAIL] %s\n", name); } \
    fflush(stdout); \
} while(0)

/* Generate a sine wave: freq Hz, sample_rate, n_samples */
static void gen_sine(short *buf, size_t n, float freq, int sample_rate) {
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)sample_rate;
        float v = sinf(2.0f * 3.14159265f * freq * t) * 0.5f;
        buf[i] = (short)(v * 32767.0f);
    }
}

/* Generate silence */
static void gen_silence(short *buf, size_t n) {
    memset(buf, 0, n * sizeof(short));
}

/* Generate noise (unvoiced) */
static void gen_noise(short *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        /* Combine two rand() calls for full range sign variation */
        int r1 = rand();
        int r2 = rand();
        short v = (short)((r1 - (RAND_MAX / 2)) + (r2 - (RAND_MAX / 2)));
        buf[i] = v;
    }
}

static int test_sine_pitch(void) {
    Emotion *e = wubu_emotion_open();
    if (!e) return 0;

    short frame[320];  /* 20ms at 16kHz */
    gen_sine(frame, 320, 200.0f, 16000);  /* 200Hz tone */

    EmotionFeatures f;
    if (wubu_emotion_frame(e, frame, 320, &f) != 0) { wubu_emotion_close(e); return 0; }

    /* Pitch should be close to 200 Hz */
    if (f.pitch_hz < 180.0f || f.pitch_hz > 220.0f) {
        printf("    Expected ~200Hz, got %.1fHz\n", f.pitch_hz);
        wubu_emotion_close(e);
        return 0;
    }

    /* Energy should be non-zero */
    if (f.energy < 0.1f) {
        printf("    Expected energy > 0.1, got %.4f\n", f.energy);
        wubu_emotion_close(e);
        return 0;
    }

    wubu_emotion_close(e);
    return 1;
}

static int test_silence(void) {
    Emotion *e = wubu_emotion_open();
    if (!e) return 0;

    short frame[320];
    gen_silence(frame, 320);

    EmotionFeatures f;
    wubu_emotion_frame(e, frame, 320, &f);

    /* Energy should be ~0 */
    if (f.energy > 0.01f) {
        printf("    Expected ~0 energy, got %.4f\n", f.energy);
        wubu_emotion_close(e);
        return 0;
    }

    /* No pitch in silence */
    if (f.pitch_hz > 10.0f) {
        printf("    Expected ~0 pitch, got %.1fHz\n", f.pitch_hz);
        wubu_emotion_close(e);
        return 0;
    }

    /* Should be neutral or thinking */
    if (f.mood != MOOD_NEUTRAL && f.mood != MOOD_THINKING) {
        printf("    Expected neutral/thinking, got %s\n", wubu_mood_name(f.mood));
        wubu_emotion_close(e);
        return 0;
    }

    wubu_emotion_close(e);
    return 1;
}

static int test_noise_unvoiced(void) {
    Emotion *e = wubu_emotion_open();
    if (!e) return 0;

    short frame[320];
    gen_noise(frame, 320);

    EmotionFeatures f;
    wubu_emotion_frame(e, frame, 320, &f);

    /* High ZCR for noise */
    if (f.zero_crossing_rate < 2.0f) {
        printf("    Expected high ZCR (>2) for noise, got %.1f\n", f.zero_crossing_rate);
        wubu_emotion_close(e);
        return 0;
    }

    /* Low pitch for noise */
    if (f.pitch_hz > 80.0f) {
        printf("    Expected low pitch for noise, got %.1fHz\n", f.pitch_hz);
        wubu_emotion_close(e);
        return 0;
    }

    wubu_emotion_close(e);
    return 1;
}

static int test_smoothing(void) {
    Emotion *e = wubu_emotion_open();
    if (!e) return 0;

    short frame[320];
    gen_sine(frame, 320, 220.0f, 16000);

    EmotionFeatures frame_f, smoothed;
    wubu_emotion_frame(e, frame, 320, &frame_f);
    wubu_emotion_smooth(e, &frame_f, &smoothed, 0.3);

    /* Smoothed pitch should be close to frame pitch */
    if (fabs(smoothed.pitch_hz - frame_f.pitch_hz) > 50.0f) {
        printf("    Smoothing diverged too much: %.1f vs %.1f\n",
               smoothed.pitch_hz, frame_f.pitch_hz);
        wubu_emotion_close(e);
        return 0;
    }

    wubu_emotion_close(e);
    return 1;
}

static int test_mood_names(void) {
    /* Verify all mood names are non-NULL */
    const char *names[] = {
        wubu_mood_name(MOOD_NEUTRAL),
        wubu_mood_name(MOOD_HAPPY),
        wubu_mood_name(MOOD_SAD),
        wubu_mood_name(MOOD_ANGRY),
        wubu_mood_name(MOOD_THINKING),
        wubu_mood_name(MOOD_EXCITED),
        wubu_mood_name(MOOD_CONFUSED),
        wubu_mood_name(MOOD_COUNT),
    };

    for (int i = 0; i < 8; i++) {
        if (!names[i]) return 0;
    }
    return 1;
}

static int test_happy_classification(void) {
    /* High energy + high pitch → happy */
    EmotionFeatures f = {0};
    f.energy = 0.2f;
    f.pitch_hz = 150.0f;  /* moderate-high pitch → happy */
    f.zero_crossing_rate = 3.0f;
    f.jitter = 0.02f;
    f.shimmer = 0.03f;

    Mood m = wubu_emotion_classify(&f);
    if (m != MOOD_HAPPY) {
        printf("    Expected HAPPY, got %s\n", wubu_mood_name(m));
        return 0;
    }
    return 1;
}

static int test_sad_classification(void) {
    /* Low energy + low pitch → sad */
    EmotionFeatures f = {0};
    f.energy = 0.02f;
    f.pitch_hz = 80.0f;
    f.zero_crossing_rate = 2.0f;
    f.jitter = 0.02f;
    f.shimmer = 0.02f;

    Mood m = wubu_emotion_classify(&f);
    if (m != MOOD_SAD) {
        printf("    Expected SAD, got %s\n", wubu_mood_name(m));
        return 0;
    }
    return 1;
}

static int test_sine_100hz(void) {
    Emotion *e = wubu_emotion_open();
    if (!e) return 0;

    short frame[320];
    gen_sine(frame, 320, 100.0f, 16000);

    EmotionFeatures f;
    wubu_emotion_frame(e, frame, 320, &f);

    if (f.pitch_hz < 85.0f || f.pitch_hz > 115.0f) {
        printf("    Expected ~100Hz, got %.1fHz\n", f.pitch_hz);
        wubu_emotion_close(e);
        return 0;
    }

    wubu_emotion_close(e);
    return 1;
}

static int test_sine_300hz(void) {
    Emotion *e = wubu_emotion_open();
    if (!e) return 0;

    short frame[320];
    gen_sine(frame, 320, 300.0f, 16000);

    EmotionFeatures f;
    wubu_emotion_frame(e, frame, 320, &f);

    if (f.pitch_hz < 270.0f || f.pitch_hz > 330.0f) {
        printf("    Expected ~300Hz, got %.1fHz\n", f.pitch_hz);
        wubu_emotion_close(e);
        return 0;
    }

    wubu_emotion_close(e);
    return 1;
}

int main(void) {
    printf("=== wubu_emotion C11 Test Suite ===\n\n");
    fflush(stdout);

    RUN_TEST("sine wave pitch detection (200Hz)", test_sine_pitch);
    RUN_TEST("silence detection", test_silence);
    RUN_TEST("noise/unvoiced detection", test_noise_unvoiced);
    RUN_TEST("feature smoothing", test_smoothing);
    RUN_TEST("mood name lookup", test_mood_names);
    RUN_TEST("happy mood classification", test_happy_classification);
    RUN_TEST("sad mood classification", test_sad_classification);
    RUN_TEST("sine wave pitch detection (100Hz)", test_sine_100hz);
    RUN_TEST("sine wave pitch detection (300Hz)", test_sine_300hz);

    printf("\n=== Results: %d/%d tests passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}

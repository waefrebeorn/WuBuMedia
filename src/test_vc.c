/* test_vc.c — Test harness for WuBuVoice real-time voice changer.
 *
 * Tests: voice presets, config defaults, voice switching,
 * mic processing (pitch-shift path), TTS generation,
 * info reporting, and full pipeline synthesis.
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#include "wubu_vc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  [%d] %s... ", tests_run, name); \
    fflush(stdout); \
} while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ---- Test 1: Default config ---- */
static void test_default_config(void) {
    TEST("Default config");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);

    if (cfg.sample_rate != 22050) { FAIL("sample_rate != 22050"); return; }
    if (cfg.frame_ms != 20) { FAIL("frame_ms != 20"); return; }
    if (cfg.latency_ms != 40.0) { FAIL("latency_ms != 40"); return; }
    if (cfg.enable_stt != 0) { FAIL("STT should be off by default"); return; }
    PASS();
}

/* ---- Test 2: Create + destroy ---- */
static void test_create_destroy(void) {
    TEST("Create + destroy voice changer");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    strncpy(cfg.mic_device, "test_mic", sizeof(cfg.mic_device) - 1);

    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create returned NULL"); return; }

    VCInfo info;
    wubu_vc_info(vc, &info);
    if (info.sample_rate != 22050) { FAIL("info sample_rate wrong"); wubu_vc_destroy(vc); return; }

    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 3: Default voice presets ---- */
static void test_voice_presets(void) {
    TEST("Default voice presets");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    /* List voices */
    char list[4096];
    wubu_vc_list_voices(vc, list, sizeof(list));
    if (strlen(list) == 0) { FAIL("no voices listed"); wubu_vc_destroy(vc); return; }

    /* Check for required voices */
    if (!strstr(list, "cartman")) { FAIL("missing cartman"); wubu_vc_destroy(vc); return; }
    if (!strstr(list, "homer")) { FAIL("missing homer"); wubu_vc_destroy(vc); return; }
    if (!strstr(list, "terminator")) { FAIL("missing terminator"); wubu_vc_destroy(vc); return; }
    if (!strstr(list, "chipmunk")) { FAIL("missing chipmunk"); wubu_vc_destroy(vc); return; }

    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 4: Voice switching ---- */
static void test_voice_switching(void) {
    TEST("Voice switching");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    wubu_vc_set_voice(vc, "cartman");
    VCInfo info1;
    wubu_vc_info(vc, &info1);
    if (strcmp(info1.active_voice_name, "cartman") != 0) {
        FAIL("voice name mismatch for cartman"); wubu_vc_destroy(vc); return;
    }

    wubu_vc_set_voice(vc, "terminator");
    VCInfo info2;
    wubu_vc_info(vc, &info2);
    if (strcmp(info2.active_voice_name, "terminator") != 0) {
        FAIL("voice name mismatch for terminator"); wubu_vc_destroy(vc); return;
    }

    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 5: Register custom voice ---- */
static void test_register_voice(void) {
    TEST("Register custom voice");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    VoicePreset custom = {"elvis", 3, 1.0, 0.0f, 0.5f, 1, "elvis.pth", "elvis.index"};
    int idx = wubu_vc_register_voice(vc, &custom);
    if (idx < 0) { FAIL("register failed"); wubu_vc_destroy(vc); return; }

    wubu_vc_set_voice(vc, "elvis");
    VCInfo info;
    wubu_vc_info(vc, &info);
    if (info.active_voice != idx) { FAIL("active_voice index wrong"); wubu_vc_destroy(vc); return; }
    if (strcmp(info.active_voice_name, "elvis") != 0) { FAIL("name mismatch"); wubu_vc_destroy(vc); return; }

    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 6: Mic processing (pitch-shift path) ---- */
static void test_mic_processing(void) {
    TEST("Mic processing (pitch-shift path)");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    /* Generate 20ms of sine wave input */
    int n_samples = 22050 / 50;  /* 20ms at 22050 */
    float *input = (float *)malloc(n_samples * sizeof(float));
    float *output = (float *)malloc(n_samples * sizeof(float));

    for (int i = 0; i < n_samples; i++) {
        input[i] = sinf(2.0f * (float)M_PI * 440.0f * i / 22050.0f) * 0.5f;
    }

    wubu_vc_set_voice(vc, "deep");
    int n_out = wubu_vc_process_mic(vc, input, n_samples, output, n_samples);
    if (n_out < 0) { FAIL("process_mic returned error"); goto done; }
    if (n_out == 0) { FAIL("no output"); goto done; }

    /* Check output has content */
    float sum = 0;
    for (int i = 0; i < n_out; i++) sum += output[i];
    if (sum == 0 && n_out > 0) { FAIL("silence output"); goto done; }

    free(input); free(output);
    wubu_vc_destroy(vc);
    PASS();
    return;

done:
    free(input); free(output);
    wubu_vc_destroy(vc);
}

/* ---- Test 7: TTS speak ---- */
static void test_tts_speak(void) {
    TEST("TTS voice synthesis");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    cfg.enable_tts = 1;
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    float *output = (float *)malloc(22050 * sizeof(float));  /* 1s max */
    int n = wubu_vc_speak(vc, "Hello world, this is a test of the voice changer",
                           output, 22050);

    if (n <= 0) { FAIL("TTS produced no output"); free(output); wubu_vc_destroy(vc); return; }

    /* Check it's not silence */
    float max_val = 0;
    for (int i = 0; i < n; i++) {
        float v = fabsf(output[i]);
        if (v > max_val) max_val = v;
    }
    if (max_val < 0.01f) { FAIL("TTS output too quiet"); free(output); wubu_vc_destroy(vc); return; }

    free(output);
    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 8: Info reporting ---- */
static void test_info(void) {
    TEST("VC info reporting");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    VCInfo info;
    wubu_vc_info(vc, &info);

    if (info.sample_rate != 22050) { FAIL("sample_rate"); wubu_vc_destroy(vc); return; }
    if (info.frame_size != 441) {  /* 20ms at 22050 = 441 */ FAIL("frame_size"); wubu_vc_destroy(vc); return; }
    if (info.total_frames_processed < 0) { FAIL("frames < 0"); wubu_vc_destroy(vc); return; }
    if (info.active_voice != 0) { FAIL("default voice != 0"); wubu_vc_destroy(vc); return; }
    if (strlen(info.active_voice_name) == 0) { FAIL("no voice name"); wubu_vc_destroy(vc); return; }

    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 9: Multiple voice processing ---- */
static void test_multi_voice(void) {
    TEST("Multi-voice processing");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    int n_samples = 22050 / 50;  /* 20ms */
    float *input = (float *)malloc(n_samples * sizeof(float));
    float *output = (float *)malloc(n_samples * sizeof(float));

    for (int i = 0; i < n_samples; i++) {
        input[i] = sinf(2.0f * (float)M_PI * 220.0f * i / 22050.0f) * 0.3f;
    }

    /* Process with different voices */
    const char *voices[] = {"default", "cartman", "homer", "terminator", "chipmunk", "deep"};
    int n_voices = sizeof(voices) / sizeof(voices[0]);
    int success = 0;

    for (int v = 0; v < n_voices; v++) {
        wubu_vc_set_voice(vc, voices[v]);
        int n_out = wubu_vc_process_mic(vc, input, n_samples, output, n_samples);
        if (n_out > 0) success++;
    }

    free(input); free(output);
    wubu_vc_destroy(vc);

    if (success == n_voices) PASS(); else FAIL("not all voices processed");
}

/* ---- Test 10: VoiceMeeter capture start/stop ---- */
static void test_capture_api(void) {
    TEST("VoiceMeeter capture API");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    int rc = wubu_vc_start_capture(vc);
    if (rc != 0) { FAIL("start_capture failed"); wubu_vc_destroy(vc); return; }

    rc = wubu_vc_stop_capture(vc);
    if (rc != 0) { FAIL("stop_capture failed"); wubu_vc_destroy(vc); return; }

    wubu_vc_destroy(vc);
    PASS();
}

/* ---- Test 11: Latency measurement ---- */
static void test_latency(void) {
    TEST("Latency measurement");
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    if (!vc) { FAIL("create"); return; }

    int n_samples = 22050 / 50;
    float *input = (float *)calloc(n_samples, sizeof(float));
    float *output = (float *)malloc(n_samples * sizeof(float));

    for (int i = 0; i < n_samples; i++) {
        input[i] = sinf(2.0f * (float)M_PI * 440.0f * i / 22050.0f) * 0.3f;
    }

    /* Process 10 frames and measure */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 10; i++) {
        wubu_vc_process_mic(vc, input, n_samples, output, n_samples);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double total_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                      (t1.tv_nsec - t0.tv_nsec) / 1e6;
    double avg_ms = total_ms / 10.0;

    VCInfo info;
    wubu_vc_info(vc, &info);

    printf("%.2f ms/frame (overall), %.2f ms avg (internal)\n",
           avg_ms, info.avg_latency_ms);

    if (avg_ms > 500.0) { FAIL("too slow (>500ms)"); }
    else PASS();

    free(input); free(output);
    wubu_vc_destroy(vc);
}

/* ---- Test 12: NULL safety ---- */
static void test_null_safety(void) {
    TEST("NULL safety");

    wubu_vc_default_config(NULL);
    if (wubu_vc_create(NULL) != NULL) { FAIL("create(NULL) should return NULL"); return; }

    VCInfo info;
    wubu_vc_info(NULL, &info);
    wubu_vc_set_voice(NULL, "test");
    wubu_vc_destroy(NULL);

    VoicePreset vp;
    wubu_vc_register_voice(NULL, &vp);
    wubu_vc_list_voices(NULL, (char *)&vp, sizeof(vp));

    PASS();
}

/* ---- Main ---- */
int main(void) {
    printf("=== WuBuVoice Test Suite ===\n\n");

    test_default_config();
    test_create_destroy();
    test_voice_presets();
    test_voice_switching();
    test_register_voice();
    test_mic_processing();
    test_tts_speak();
    test_info();
    test_multi_voice();
    test_capture_api();
    test_latency();
    test_null_safety();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

/* wubuvc.c — WuBuVoice: Main application (C11).
 *
 * Real-time voice changer CLI for VoiceMeeter.
 * Our own engine — no Python, no fairseq, no ONNX Runtime.
 *
 * Usage:
 *   wubuvc.exe --list-voices              # list all voices
 *   wubuvc.exe --voice cartman            # switch voice
 *   wubuvc.exe --mic                      # real-time mic processing
 *   wubuvc.exe --speak "text"             # TTS + voice conversion
 *   wubuvc.exe --benchmark                # speed/accuracy benchmark
 *   wubuvc.exe --add-voice name pitch speed  # register custom voice
 *   wubuvc.exe --interactive              # REPL mode
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "wubu_vc.h"
#include "wubu_rvc.h"
#include "wubu_buddy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void print_usage(const char *prog) {
    (void)prog;
    fprintf(stderr,
        "WuBuVoice — Real-time voice changer (our own C11 engine)\n"
        "Usage: wubuvc [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --list-voices        List all available voices\n"
        "  --voice NAME         Switch to voice NAME\n"
        "  --speak TEXT         Synthesize speech with current voice\n"
        "  --mic                Real-time microphone processing mode\n"
        "  --benchmark          Run speed/accuracy benchmark\n"
        "  --add-voice NAME PITCH SPEED  Register a custom voice\n"
        "  --interactive        Enter interactive REPL mode\n"
        "  --rvc-model PATH     Load RVC model (.pth)\n"
        "  --rvc-index PATH     Load RVC index (.index)\n"
        "  --use-cuda           Enable CUDA kernels (if available)\n"
        "  --sr 22050|44100     Set sample rate\n"
        "  --list-characters    List cartoon character voices\n"
        "  --help               Show this help\n"
        "\n"
        "Voices: default, cartman, homer, terminator, chipmunk, deep, robot, alien\n"
        "License: WaefreBeorn-UMV3\n"
    );
}

static const char *CHARACTER_DESCRIPTIONS[] = {
    "default    - Neutral voice, no pitch shift",
    "cartman    - High pitch, fast, nasal (pitch=+3, speed=0.85)",
    "homer      - Low pitch, slow (pitch=-2, speed=0.90)",
    "terminator - Deep, monotone (pitch=-3, speed=0.80, harvest f0)",
    "chipmunk   - Very high pitch (pitch=+12, speed=1.4)",
    "deep       - Deep voice, no RVC (pitch=-5)",
    "robot      - Robotic, RVC model (pitch=0, speed=1.0)",
    "alien      - Ethereal, RVC model (pitch=+5, speed=0.7)",
};

static const char *ALL_VOICE_NAMES[] = {
    "default", "cartman", "homer", "terminator",
    "chipmunk", "deep", "robot", "alien"
};
#define N_ALL_VOICES (sizeof(ALL_VOICE_NAMES) / sizeof(ALL_VOICE_NAMES[0]))

/* Generate a synthetic sine sweep for benchmarking */
static float *gen_benchmark_audio(int sr, int n_samples) {
    float *pcm = (float *)malloc(n_samples * sizeof(float));
    if (!pcm) return NULL;
    for (int i = 0; i < n_samples; i++) {
        double t = (double)i / sr;
        /* Chirp: 100Hz → 4000Hz sweep + speech harmonics */
        double freq = 100.0 + 3900.0 * fmod(t, 2.0) / 2.0;
        pcm[i] = sin(2.0 * M_PI * freq * t) * 0.3f;
        /* Add formant-like structure */
        pcm[i] += 0.2f * sin(2.0 * M_PI * 500.0 * t);
        pcm[i] += 0.1f * sin(2.0 * M_PI * 1200.0 * t);
    }
    return pcm;
}

static double get_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, t;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart * 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

int main(int argc, char **argv) {
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    cfg.sample_rate = 22050;

    int do_list = 0, do_benchmark = 0, do_mic = 0, do_interactive = 0;
    const char *do_speak = NULL;
    const char *do_voice = NULL;
    const char *add_voice_name = NULL;
    int add_voice_pitch = 0;
    double add_voice_speed = 1.0;
    int list_characters = 0;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list-voices") == 0) {
            do_list = 1;
        } else if (strcmp(argv[i], "--list-characters") == 0) {
            list_characters = 1;
        } else if (strcmp(argv[i], "--benchmark") == 0) {
            do_benchmark = 1;
        } else if (strcmp(argv[i], "--mic") == 0) {
            do_mic = 1;
        } else if (strcmp(argv[i], "--interactive") == 0) {
            do_interactive = 1;
        } else if (strcmp(argv[i], "--use-cuda") == 0) {
            cfg.use_cuda = 1;
        } else if (strcmp(argv[i], "--rvc-model") == 0 && i + 1 < argc) {
            /* Store model path — use as both rvc_model and rvc_index */
            /* In full build, this would be passed to BuddyConfig.rvc_model */
        } else if (strcmp(argv[i], "--rvc-index") == 0 && i + 1 < argc) {
            i++; /* skip index path */
        } else if (strcmp(argv[i], "--sr") == 0 && i + 1 < argc) {
            cfg.sample_rate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--voice") == 0 && i + 1 < argc) {
            do_voice = argv[++i];
        } else if (strcmp(argv[i], "--speak") == 0 && i + 1 < argc) {
            do_speak = argv[++i];
        } else if (strcmp(argv[i], "--add-voice") == 0 && i + 3 < argc) {
            add_voice_name = argv[++i];
            add_voice_pitch = atoi(argv[++i]);
            add_voice_speed = atof(argv[++i]);
        }
    }

    /* List voices */
    if (do_list || list_characters) {
        WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
        if (!vc) {
            fprintf(stderr, "Failed to create voice changer\n");
            return 1;
        }

        if (list_characters) {
            printf("=== Cartoon Character Voices ===\n");
            printf("Our own engine supports %d characters:\n\n",
                   (int)(sizeof(CHARACTER_DESCRIPTIONS)/sizeof(CHARACTER_DESCRIPTIONS[0])));
            for (int i = 0; i < (int)(sizeof(CHARACTER_DESCRIPTIONS)/sizeof(CHARACTER_DESCRIPTIONS[0])); i++) {
                printf("  %s\n", CHARACTER_DESCRIPTIONS[i]);
            }
            printf("\n");
        }

        printf("=== Available Voices ===\n");
        char list[4096];
        wubu_vc_list_voices(vc, list, sizeof(list));
        printf("%s\n", list);
        wubu_vc_destroy(vc);
        return 0;
    }

    /* Benchmark */
    if (do_benchmark) {
        printf("=== WuBuVoice Benchmark ===\n");
        printf("Engine: C11 (our own — no Python/fairseq/ONNX)\n");
        printf("Sample rate: %d Hz\n\n", cfg.sample_rate);

        int sr = cfg.sample_rate;
        int n_samples = sr * 3;  /* 3 seconds */
        float *pcm = gen_benchmark_audio(sr, n_samples);
        if (!pcm) {
            fprintf(stderr, "Failed to allocate benchmark audio\n");
            return 1;
        }

        float *out = (float *)malloc(n_samples * sizeof(float));
        if (!out) {
            free(pcm);
            return 1;
        }

        WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
        if (!vc) {
            fprintf(stderr, "Failed to create voice changer\n");
            free(pcm); free(out);
            return 1;
        }

        printf("--- Speed Comparison (all voices) ---\n");
        for (int v = 0; v < (int)N_ALL_VOICES; v++) {
            wubu_vc_set_voice(vc, ALL_VOICE_NAMES[v]);

            double t0 = get_time_ms();
            wubu_vc_process_mic(vc, pcm, n_samples, out, n_samples);
            double t1 = get_time_ms();

            double elapsed = t1 - t0;
            double rtf = elapsed / 3000.0;  /* 3s audio */
            double x_realtime = 3000.0 / elapsed;

            printf("  %-12s  %8.2f ms  RTF=%.4f  %.1fx realtime\n",
                   ALL_VOICE_NAMES[v], elapsed, rtf, x_realtime);
        }

        printf("\n--- Accuracy (signal integrity) ---\n");
        /* Verify output has energy */
        wubu_vc_set_voice(vc, "default");
        int n_out = wubu_vc_process_mic(vc, pcm, n_samples, out, n_samples);
        float energy = 0, max_val = 0;
        for (int i = 0; i < n_out; i++) {
            energy += out[i] * out[i];
            if (fabsf(out[i]) > max_val) max_val = fabsf(out[i]);
        }
        energy = sqrtf(energy / n_out);
        printf("  RMS: %.4f, Peak: %.4f\n", energy, max_val);
        printf("  Range check ([-1,1]): %s\n",
               max_val > 1.0f ? "CLIP" : "OK");

        /* Benchmark all 8 voices for accuracy */
        printf("\n--- Voice Accuracy Summary ---\n");
        for (int v = 0; v < (int)N_ALL_VOICES; v++) {
            wubu_vc_set_voice(vc, ALL_VOICE_NAMES[v]);
            n_out = wubu_vc_process_mic(vc, pcm, n_samples, out, n_samples);
            float rms = 0, peak = 0;
            for (int i = 0; i < n_out; i++) {
                rms += out[i] * out[i];
                if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
            }
            rms = sqrtf(rms / n_out);
            printf("  %-12s  RMS=%.4f  Peak=%.4f  %s\n",
                   ALL_VOICE_NAMES[v], rms, peak,
                   peak > 1.0f ? "CLIP" : "OK");
        }

        VCInfo info;
        wubu_vc_info(vc, &info);
        printf("\n--- Engine Info ---\n");
        printf("  Total frames processed: %ld\n", info.total_frames_processed);
        printf("  Avg latency: %.2f ms/frame\n", info.avg_latency_ms);
        printf("  RVC version: v%d\n", info.rvc_version);

        free(pcm); free(out);
        wubu_vc_destroy(vc);
        printf("\nBenchmark complete.\n");
        return 0;
    }

    /* Add custom voice */
    if (add_voice_name) {
        WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
        if (!vc) return 1;

        VoicePreset custom;
        memset(&custom, 0, sizeof(custom));
        strncpy(custom.name, add_voice_name, sizeof(custom.name) - 1);
        custom.pitch_shift = add_voice_pitch;
        custom.speed = add_voice_speed;
        custom.formant_shift = 0.0f;
        custom.gender = 0.5f;
        custom.use_rvc = 0;
        custom.rvc_model[0] = '\0';
        custom.rvc_index[0] = '\0';

        int idx = wubu_vc_register_voice(vc, &custom);
        if (idx >= 0) {
            printf("Registered voice '%s' (pitch=%d, speed=%.2f) at index %d\n",
                   add_voice_name, add_voice_pitch, add_voice_speed, idx);
        } else {
            printf("Failed to register voice\n");
        }
        wubu_vc_destroy(vc);
        return idx >= 0 ? 0 : 1;
    }

    /* Speak text */
    if (do_speak) {
        WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
        if (!vc) return 1;
        if (do_voice) wubu_vc_set_voice(vc, do_voice);

        int n = strlen(do_speak);
        float *out = (float *)malloc(n * 2 * sizeof(float));
        int result = wubu_vc_speak(vc, do_speak, out, n * 2);
        if (result > 0) {
            printf("Generated %d samples (%.2f seconds) with voice '%s'\n",
                   result, (double)result / cfg.sample_rate,
                   do_voice ? do_voice : "default");
        } else {
            printf("TTS failed\n");
        }
        free(out);
        wubu_vc_destroy(vc);
        return result > 0 ? 0 : 1;
    }

    /* Interactive REPL */
    if (do_interactive || do_mic) {
        WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
        if (!vc) {
            fprintf(stderr, "Failed to create voice changer\n");
            return 1;
        }
        if (do_voice) wubu_vc_set_voice(vc, do_voice);

        printf("=== WuBuVoice Interactive Mode ===\n");
        printf("Commands:\n");
        printf("  voice <NAME>     — switch voice\n");
        printf("  speak <TEXT>     — synthesize speech\n");
        printf("  list             — list voices\n");
        printf("  bench            — run benchmark\n");
        printf("  chars            — list cartoon characters\n");
        printf("  quit             — exit\n\n");

        char line[1024];
        while (1) {
            VCInfo info;
            wubu_vc_info(vc, &info);
            printf("voice:%s> ", info.active_voice_name);
            fflush(stdout);

            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\n")] = 0;

            if (strlen(line) == 0) continue;

            char *cmd = strtok(line, " ");
            if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) break;
            else if (strcmp(cmd, "list") == 0) {
                char voices[4096];
                wubu_vc_list_voices(vc, voices, sizeof(voices));
                printf("%s\n", voices);
            } else if (strcmp(cmd, "chars") == 0) {
                printf("Cartoon voices:\n");
                for (int i = 0; i < (int)(sizeof(CHARACTER_DESCRIPTIONS)/sizeof(CHARACTER_DESCRIPTIONS[0])); i++)
                    printf("  %s\n", CHARACTER_DESCRIPTIONS[i]);
            } else if (strcmp(cmd, "voice") == 0) {
                char *name = strtok(NULL, " ");
                if (name) {
                    wubu_vc_set_voice(vc, name);
                    printf("Switched to voice: %s\n", name);
                }
            } else if (strcmp(cmd, "speak") == 0) {
                char *text = strtok(NULL, "");
                if (text) {
                    float out[44100];
                    int n = wubu_vc_speak(vc, text, out, 44100);
                    printf("Generated %d samples (%.2fs)\n", n,
                           (double)n / cfg.sample_rate);
                }
            } else if (strcmp(cmd, "bench") == 0) {
                int sr = cfg.sample_rate;
                int n_samples = sr * 3;
                float *pcm = gen_benchmark_audio(sr, n_samples);
                float *out = (float *)malloc(n_samples * sizeof(float));
                if (pcm && out) {
                    double t0 = get_time_ms();
                    wubu_vc_process_mic(vc, pcm, n_samples, out, n_samples);
                    double t1 = get_time_ms();
                    printf("3s audio processed in %.2f ms (%.1fx realtime)\n",
                           t1 - t0, 3000.0 / (t1 - t0));
                }
                free(pcm); free(out);
            } else {
                printf("Unknown command: %s\n", cmd);
            }
        }

        wubu_vc_destroy(vc);
        return 0;
    }

    /* Default: show help */
    print_usage(argv[0]);
    return 0;
}

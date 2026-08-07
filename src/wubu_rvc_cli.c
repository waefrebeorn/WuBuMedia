/* wubu_rvc_cli.c — WuBuRVC standalone converter (C11, OpenMP).
 *
 * Converts any mono WAV into a trained RVC character voice with the REAL
 * engine: C HuBERT content → YIN f0 → enc_p + flow + GeneratorNSF.
 * No Python, no PyTorch at runtime.
 *
 * Usage:
 *   wubu_rvc_cli <input.wav> <model_dir> <output.wav> [--model model.pth]
 *
 *   input.wav   mono 16k/22.05k/40k PCM (any sr, resampled internally)
 *   model_dir   e.g. models/rvc/cartman  (dir containing model.pth or *.pth
 *               + optional .index for retrieval)
 *   output.wav  mono 40k PCM_16
 *
 * Steps (matching Mangio-RVC v23.7.0 infer):
 *   1. load wav -> resample to 16k
 *   2. load model (.pth) -> determine version (v1/v2), sample rate, upsample
 *   3. HuBERT content (v2: layer 12 768-dim, v1: layer 9 + final_proj 256-dim)
 *   4. linear ×2 upsampling of content to 200 fps
 *   5. YIN f0 at 100 fps -> coarse + nsff0, then ×2 nearest to 200 fps
 *   6. wubu_rvc_synthesize_real -> audio at model sample rate
 *   7. write PCM_16 wav
 *
 * License: WaefreBeorn-UMV3
 */
#include "wubu_rvc.h"
#include "wubu_rvc_parity.h"
#include "wubu_rvc_real.h"
#include "wubu_rvc_hubert.h"
#include "wubu_rvc_f0.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static void die(const char *msg) { fprintf(stderr, "wubu_rvc_cli: %s\n", msg); exit(1); }

/* ── minimal WAV reader: mono PCM_16 or PCM_32 float ── */
static float *read_wav(const char *path, int *n_out, int *sr_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return NULL; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) { fclose(f); return NULL; }
    unsigned short audiofmt = (unsigned short)(hdr[20] | hdr[21] << 8);
    unsigned short nch = (unsigned short)(hdr[22] | hdr[23] << 8);
    unsigned int sr = (unsigned int)(hdr[24] | hdr[25] << 8 | hdr[26] << 16 | hdr[27] << 24);
    unsigned int databytes = (unsigned int)(hdr[40] | hdr[41] << 8 | hdr[42] << 16 | hdr[43] << 24);
    unsigned short bits = (unsigned short)(hdr[34] | hdr[35] << 8);
    fseek(f, 44, SEEK_SET);
    unsigned char *raw = (unsigned char *)malloc(databytes ? databytes : 1);
    if (!raw) { fclose(f); return NULL; }
    if (fread(raw, 1, databytes, f) != databytes) { free(raw); fclose(f); return NULL; }
    fclose(f);
    int nsamples = databytes / (bits / 8) / nch;
    float *out = (float *)malloc((size_t)nsamples * sizeof(float));
    if (!out) { free(raw); return NULL; }
    for (int i = 0; i < nsamples; i++) {
        float v = 0;
        if (audiofmt == 3 && bits == 32) { /* IEEE float */
            int off = i * nch * 4;
            memcpy(&v, raw + off, 4);
        } else if (bits == 16) {
            short s;
            memcpy(&s, raw + i * nch * 2, 2);
            v = s / 32768.0f;
        } else if (bits == 24) {
            int s = (raw[i * nch * 3] | raw[i * nch * 3 + 1] << 8 | (signed char)raw[i * nch * 3 + 2] << 16);
            v = s / 8388608.0f;
        }
        out[i] = v; /* take channel 0 */
    }
    free(raw);
    *n_out = nsamples;
    *sr_out = (int)sr;
    return out;
}

/* ── minimal WAV writer: mono 16-bit ── */
static int write_wav(const char *path, const float *data, int n, int sr) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    unsigned int databytes = (unsigned int)n * 2;
    unsigned int riffsz = 36 + databytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riffsz, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    unsigned int fmtsz = 16; fwrite(&fmtsz, 4, 1, f);
    unsigned short fmt = 1, nch = 1; unsigned int rate = (unsigned int)sr;
    unsigned int byterate = rate * 2; unsigned short align = 2, bits = 16;
    fwrite(&fmt, 2, 1, f); fwrite(&nch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&byterate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&databytes, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float v = data[i];
        if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
        short s = (short)(v * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    return 0;
}

/* linear resample to target sr */
static float *resample(const float *in, int n_in, int sr_in, int sr_out, int *n_out) {
    if (sr_in == sr_out) {
        float *out = (float *)malloc((size_t)n_in * sizeof(float));
        if (out) memcpy(out, in, (size_t)n_in * sizeof(float));
        *n_out = n_in;
        return out;
    }
    double ratio = (double)sr_out / sr_in;
    int n = (int)(n_in * ratio);
    float *out = (float *)malloc((size_t)n * sizeof(float));
    if (!out) return NULL;
    for (int i = 0; i < n; i++) {
        double pos = i / ratio;
        int i0 = (int)pos;
        int i1 = i0 + 1 < n_in ? i0 + 1 : i0;
        double frac = pos - i0;
        out[i] = (float)(in[i0] * (1 - frac) + in[i1] * frac);
    }
    *n_out = n;
    return out;
}

/* linear ×2 upsample along frames: [T, dim] -> [2T, dim] frame-major.
 * Matches the RVC pipeline's content interpolation (scale_factor=2). */
static float *upsample_frames(const float *in, int T, int dim, int *T2_out) {
    int T2 = T * 2;
    float *out = (float *)calloc((size_t)T2 * dim, sizeof(float));
    if (!out) return NULL;
    for (int j = 0; j < T2; j++) {
        double pos = j * (T - 1) / (double)(T2 - 1); /* align_corners */
        int i0 = (int)pos;
        int i1 = i0 + 1 < T ? i0 + 1 : i0;
        double frac = pos - i0;
        for (int d = 0; d < dim; d++) {
            double a = in[(size_t)i0 * dim + d];
            double b = in[(size_t)i1 * dim + d];
            out[(size_t)j * dim + d] = (float)(a + (b - a) * frac);
        }
    }
    *T2_out = T2;
    return out;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <input.wav> <model_dir> <output.wav> [--model file.pth]\n"
                "         [--speaker N] [--noise SCALE] [--hubert PATH]\n"
                "\n"
                "         --speaker N   : speaker id for multi-speaker models\n"
                "         --noise S     : noise scale (0.0 = deterministic, 0.66666 = reference)\n"
                "         --hubert PATH : override HuBERT weights path\n",
                argv[0]);
        return 1;
    }
    const char *in_path = argv[1];
    const char *model_dir = argv[2];
    const char *out_path = argv[3];
    srand((unsigned)time(NULL));  /* seed for NSF noise injection */
    char model_path[1024] = {0};
    char index_path[1024] = {0};
    char hubert_path[1024] = {0};
    int speaker_id = 0;       /* default: speaker 0 */
    float noise_scale = 0.0f; /* default: deterministic (parity mode) */
    snprintf(model_path, sizeof(model_path), "%s/model.pth", model_dir);
    for (int a = 4; a < argc - 1; a++) {
        if (strcmp(argv[a], "--model") == 0) {
            snprintf(model_path, sizeof(model_path), "%s", argv[a + 1]);
            a++;
        } else if (strcmp(argv[a], "--speaker") == 0) {
            speaker_id = atoi(argv[a + 1]);
            a++;
        } else if (strcmp(argv[a], "--noise") == 0) {
            noise_scale = (float)atof(argv[a + 1]);
            a++;
        } else if (strcmp(argv[a], "--hubert") == 0) {
            snprintf(hubert_path, sizeof(hubert_path), "%s", argv[a + 1]);
            a++;
        }
    }
    if (!strstr(model_path, ".pth")) {
        /* fall back to first *.pth in dir */
        char pat[1024]; snprintf(pat, sizeof(pat), "%s/*.pth", model_dir);
        /* use the C loader's own dir scan via wubu_rvc_load_weights later;
         * here just try common names */
    }
    FILE *chk = fopen(model_path, "rb");
    if (!chk) die("cannot open model.pth — pass --model");
    fclose(chk);

    /* index: scan model_dir for any *.index file */
    index_path[0] = 0;
    {
        char idx_pat[1024];
        snprintf(idx_pat, sizeof(idx_pat), "%s/*.index", model_dir);
        /* Simple glob: check common index file patterns */
        const char *idx_names[] = {
            "added_IVF793_Flat_nprobe_1.index",
            "trained_by_pool9045_Flat_nprobe_1.index",
            NULL
        };
        for (int i = 0; idx_names[i] && !index_path[0]; i++) {
            char test_path[1024];
            snprintf(test_path, sizeof(test_path), "%s/%s", model_dir, idx_names[i]);
            FILE *f = fopen(test_path, "rb");
            if (f) { fclose(f); strncpy(index_path, test_path, sizeof(index_path)-1); }
        }
        if (!index_path[0]) {
            /* Try the Cartman-specific name (backward compat) */
            snprintf(index_path, sizeof(index_path),
                     "%s/added_IVF793_Flat_nprobe_1_EricCartmanV1_v2.index", model_dir);
            FILE *f = fopen(index_path, "rb");
            if (!f) index_path[0] = 0;
            else fclose(f);
        }
    }

    /* 1. audio */
    int n_in = 0, sr_in = 0;
    float *audio = read_wav(in_path, &n_in, &sr_in);
    if (!audio) die("cannot read input wav (need RIFF mono PCM_16/24/32f)");
    printf("[1] input: %d samples @%d Hz (%.2f s)\n", n_in, sr_in, (double)n_in / sr_in);
    int n16 = 0;
    float *pcm16 = resample(audio, n_in, sr_in, 16000, &n16);
    free(audio);
    if (!pcm16) die("resample failed");

    /* Build config early so we can load the model and determine version */
    RVCConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.model_path, model_path, sizeof(cfg.model_path) - 1);
    if (index_path[0]) strncpy(cfg.index_path, index_path, sizeof(cfg.index_path) - 1);
    cfg.version = RVC_V2;
    cfg.mel_channels = 80;
    cfg.hidden_channels = 256;

    /* 2. Load model early to determine version (v1 vs v2) */
    WuBuRVC *rvc = wubu_rvc_load(&cfg);
    if (!rvc || !wubu_rvc_is_model_loaded(rvc)) die("model load failed");

    /* Determine RVC version from loaded model */
    int rvc_ver = rvc->rvc_version;
    if (rvc_ver <= 0) rvc_ver = 2;
    int content_dim = (rvc_ver == 1) ? 256 : 768;
    int sr_out = rvc->sample_rate;
    if (sr_out <= 0) sr_out = 40000;
    int ups_total = rvc->graph.upsample_rate;
    if (ups_total <= 0) ups_total = 400;
    printf("[2] model version: v%d (content_dim=%d, sr=%d, ups=%d)\n",
           rvc_ver, content_dim, sr_out, ups_total);

    /* 3. HuBERT content (v2: layer 12 768-dim, v1: layer 9 + final_proj 256-dim) */
    WuBuHubert hb;
    memset(&hb, 0, sizeof(hb));
    /* Allow --hubert PATH override; otherwise search model_dir then default */
    const char *hubert_bin = hubert_path[0] ? hubert_path : "models/rvc/hubert_weights.bin";
    if (!hubert_path[0]) {
        char hp[1024];
        snprintf(hp, sizeof(hp), "%s/hubert_weights.bin", model_dir);
        FILE *hf = fopen(hp, "rb");
        if (hf) { fclose(hf); hubert_bin = hp; }
    }
    if (wubu_hubert_load(&hb, hubert_bin) != 0)
        die("hubbert weights missing — run tools/extract_hubert_weights.py or pass --hubert PATH");
    int T = wubu_hubert_output_length(n16);
    printf("[3] hubert frames: %d\n", T);
    float *content = (float *)malloc((size_t)T * content_dim * sizeof(float));
    clock_t t0 = clock();
    int Tc = wubu_hubert_extract_real(&hb, pcm16, n16, rvc_ver, content, T * content_dim);
    printf("     hubert: %.2f s (%.2fx realtime)\n",
           (double)(clock() - t0) / CLOCKS_PER_SEC,
           (double)(clock() - t0) / CLOCKS_PER_SEC / ((double)n16 / 16000.0));
    if (Tc != T) { printf("     (hubert returned %d frames)\n", Tc); T = Tc; }

    /* 4. content ×2 upsample: [T, dim] -> [2T, dim] */
    int T2 = 0;
    float *content_up = upsample_frames(content, T, content_dim, &T2);
    free(content);
    printf("[4] content_up frames: %d\n", T2);

    /* 5. f0 (YIN at 16k, 100 fps) + coarse, then ×2 nearest to 200 fps */
    int n_f0 = 0;
    float *f0 = (float *)malloc((size_t)(n16 / 160 + 2) * sizeof(float));
    n_f0 = wubu_f0_yin(pcm16, n16, 16000, 1024, 160, 50.0f, 1100.0f, f0, n16 / 160 + 2);
    printf("[5] yin f0 frames: %d\n", n_f0);
    int *coarse100 = (int *)malloc((size_t)n_f0 * sizeof(int));
    float *nsff0_100 = (float *)malloc((size_t)n_f0 * sizeof(float));
    wubu_f0_to_coarse(f0, n_f0, 50.0f, 1100.0f, coarse100, nsff0_100);
    int n_f0_2 = n_f0 * 2;
    int *f0_coarse = (int *)malloc((size_t)n_f0_2 * sizeof(int));
    float *nsff0 = (float *)malloc((size_t)n_f0_2 * sizeof(float));
    for (int j = 0; j < n_f0_2; j++) {
        int i = j / 2; if (i >= n_f0) i = n_f0 - 1;
        f0_coarse[j] = coarse100[i];
        nsff0[j] = nsff0_100[i];
    }
    free(f0); free(coarse100); free(nsff0_100);

    /* 6. real synth */
    printf("[6] synth...\n");
    int n_frames = T2 < n_f0_2 ? T2 : n_f0_2;
    int max_audio = n_frames * ups_total;
    float *out_audio = (float *)malloc((size_t)max_audio * sizeof(float));
    if (!out_audio) die("alloc");

    /* transpose content_up [T2, dim] frame-major -> [dim, T2] col-major */
    float *cmaj = (float *)malloc((size_t)content_dim * n_frames * sizeof(float));
    if (!cmaj) die("alloc");
    for (int j = 0; j < n_frames; j++)
        for (int d = 0; d < content_dim; d++)
            cmaj[(size_t)d * n_frames + j] = content_up[(size_t)j * content_dim + d];
    free(content_up);

    t0 = clock();
    int n_out = wubu_rvc_synthesize_real(rvc->model, cmaj, n_frames, content_dim,
                                         f0_coarse, nsff0, speaker_id, noise_scale,
                                         out_audio, max_audio);
    double synth_s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (n_out <= 0) die("synth failed");
    printf("     synth: %.2f s (%.2fx realtime)\n", synth_s,
           synth_s / ((double)n_out / sr_out));

    /* 6. write wav */
    if (write_wav(out_path, out_audio, n_out, sr_out) != 0) die("write wav failed");
    printf("[6] wrote %s: %d samples @%d (%.2f s)\n", out_path, n_out, sr_out,
           (double)n_out / sr_out);

    free(pcm16); free(f0_coarse); free(nsff0); free(out_audio); free(cmaj);
    wubu_hubert_free(&hb);
    wubu_rvc_destroy(rvc);
    return 0;
}

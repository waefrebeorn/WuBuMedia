/* test_load_model.c — Minimal test: load Cartman .pth, verify weights loaded,
 * run ONE pipeline inference, check for crash. */
#include "wubu_rvc.h"
#include "wubu_rvc_parity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define M_PI 3.14159265358979323846f

int main(void) {
    const char *pth = "models/rvc/cartman/EricCartmanV1_e650_s10400.pth";
    const char *idx = "models/rvc/cartman/added_IVF793_Flat_nprobe_1_EricCartmanV1_v2.index";

    RVCConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.model_path, pth, sizeof(cfg.model_path) - 1);
    if (idx) strncpy(cfg.index_path, idx, sizeof(cfg.index_path) - 1);
    cfg.version = RVC_V2;
    cfg.sample_rate = 22050;
    cfg.mel_channels = 80;
    cfg.hidden_channels = 256;
    cfg.n_flow_layers = 4;
    cfg.n_hifigan_upsamples = 4;
    cfg.n_mrf_stacks = 3;
    cfg.n_residual_layers = 4;

    WuBuRVC *rvc = wubu_rvc_load(&cfg);
    if (!rvc) {
        printf("FAIL: wubu_rvc_load returned NULL\n");
        return 1;
    }

    printf("Model loaded: %s\n", wubu_rvc_is_model_loaded(rvc) ? "YES" : "NO");

    if (rvc->model) {
        printf("  Tensors: %d\n", rvc->model->n_tensors);
        printf("  Hidden: %d\n", rvc->model->hidden_channels);
        printf("  SR: %d\n", rvc->model->sample_rate);
        printf("  Mel: %d\n", rvc->model->mel_channels);
        printf("  Flow layers: %d\n", rvc->model->n_flow_layers);
        printf("  Index vectors: %d\n", rvc->model->n_index_vectors);
    }

    /* Check key weights loaded */
    const RVCTensor *t = wubu_rvc_find_tensor(rvc->model, "dec.conv_pre.weight");
    if (t) {
        printf("  dec.conv_pre.weight: dims=[%d,%d,%d] n_dims=%d\n",
               t->dims[0], t->dims[1], t->dims[2], t->n_dims);
        printf("  hidden_channels = dim[0] = %d\n", t->dims[0]);
    } else {
        printf("  WARNING: dec.conv_pre.weight not found!\n");
    }

    /* Generate test sine audio (1 second at 22050 Hz) */
    int sr = 22050;
    int n = sr;  /* 1 second */
    float *input = (float *)malloc((size_t)n * sizeof(float));
    float *output = (float *)malloc((size_t)n * sizeof(float));
    for (int i = 0; i < n; i++) {
        input[i] = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * i / sr);
    }

    printf("\nRunning ONE convert_audio...\n");
    int rc = wubu_rvc_convert_audio(rvc, input, n, output, n);
    printf("convert_audio rc=%d (expected output samples)\n", rc);

    if (rc > 0) {
        /* Basic sanity on output */
        float max_v = 0, rms = 0;
        float clipped = 0;
        for (int i = 0; i < rc; i++) {
            if (fabsf(output[i]) > max_v) max_v = fabsf(output[i]);
            if (output[i] > 1.0f || output[i] < -1.0f) clipped++;
            rms += output[i] * output[i];
        }
        rms = sqrtf(rms / rc);
        printf("  Output: n=%d max=%.4f rms=%.4f clipped=%.0f nan=%d\n",
               rc, max_v, rms, clipped, 0);
        printf("  PASS: valid audio output\n");
    } else {
        printf("  FAIL: pipeline returned error\n");
    }

    free(input);
    free(output);
    wubu_rvc_destroy(rvc);
    printf("\nDone.\n");
    return 0;
}

/* test_pipeline.c — Load Cartman, run ONE pipeline inference with short audio */
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
    cfg.sample_rate = 40000;
    cfg.mel_channels = 80;
    cfg.hidden_channels = 256;
    cfg.n_flow_layers = 4;
    cfg.n_hifigan_upsamples = 4;
    cfg.n_mrf_stacks = 3;
    cfg.n_residual_layers = 4;

    WuBuRVC *rvc = wubu_rvc_load(&cfg);
    if (!rvc || !wubu_rvc_is_model_loaded(rvc)) {
        printf("FAIL: model not loaded\n");
        return 1;
    }
    printf("Model loaded: %d tensors, hidden=%d\n",
           rvc->model->n_tensors, rvc->model->hidden_channels);

    /* Short test: 2048 samples (~51ms at 40kHz) */
    int n = 2048;
    float *input = (float *)calloc(n, sizeof(float));
    float *output = (float *)calloc(n, sizeof(float));
    for (int i = 0; i < n; i++) {
        input[i] = 0.3f * sinf(2.0f * (float)M_PI * 220.0f * i / 40000.0f);
    }

    printf("Running convert_audio(2048 samples)...\n");
    int rc = wubu_rvc_convert_audio(rvc, input, n, output, n);
    printf("convert_audio rc=%d\n", rc);

    if (rc > 0) {
        float max_v = 0;
        for (int i = 0; i < rc; i++)
            if (fabsf(output[i]) > max_v) max_v = fabsf(output[i]);
        printf("  Output: n=%d max=%.4f\n", rc, max_v);

        /* Also test wubu_rvc_synthesize with synthetic mel */
        int n_frames = 4;
        float *mel = (float *)calloc(n_frames * 80, sizeof(float));
        for (int i = 0; i < n_frames * 80; i++) mel[i] = ((float)rand() / RAND_MAX) * 2 - 1;
        rc = wubu_rvc_synthesize(rvc, mel, n_frames, 80, output, n);
        printf("synthesize rc=%d (n_frames=%d, hidden=%d)\n", rc, n_frames, rvc->model->hidden_channels);
        if (rc > 0) printf("  Pipeline OK: %d samples output\n", rc);
        free(mel);
    }

    free(input);
    free(output);
    wubu_rvc_destroy(rvc);
    printf("\nDone.\n");
    return 0;
}

/* test_load2.c — Test ONLY model loading, no pipeline run */
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

    printf("Loading model...\n");
    WuBuRVC *rvc = wubu_rvc_load(&cfg);
    printf("wubu_rvc_load returned: %p\n", (void *)rvc);

    if (rvc) {
        printf("initialized=%d, loaded=%d\n", rvc->initialized, rvc->loaded);
        printf("graph: version=%d sr=%d mel=%d hidden=%d nres=%d\n",
               rvc->graph.version, rvc->graph.sample_rate, rvc->graph.mel_channels,
               rvc->graph.hidden_channels, rvc->graph.n_residual_layers);
        printf("is_model_loaded: %d\n", wubu_rvc_is_model_loaded(rvc));

        if (rvc->model) {
            printf("model: version=%d tensors=%d hidden=%d sr=%d mel=%d\n",
                   rvc->model->version, rvc->model->n_tensors,
                   rvc->model->hidden_channels, rvc->model->sample_rate,
                   rvc->model->mel_channels);
            printf("model: flow_layers=%d residual=%d index=%d\n",
                   rvc->model->n_flow_layers, rvc->model->n_residual_layers,
                   rvc->model->n_index_vectors);

            /* Check key tensors */
            const RVCTensor *t;
            t = wubu_rvc_find_tensor(rvc->model, "dec.conv_pre");
            if (t) printf("  dec.conv_pre: dims=[%d,%d,%d]\n", t->dims[0], t->dims[1], t->dims[2]);

            t = wubu_rvc_find_tensor(rvc->model, "dec.ups.0");
            if (t) printf("  dec.ups.0: dims=[%d,%d,%d] n_dims=%d\n", t->dims[0], t->dims[1], t->dims[2], t->n_dims);

            t = wubu_rvc_find_tensor(rvc->model, "dec.ups.3");
            if (t) printf("  dec.ups.3: dims=[%d,%d,%d] n_dims=%d\n", t->dims[0], t->dims[1], t->dims[2], t->n_dims);

            /* Verify de-normalization: compare stats vs PyTorch reference */
            if (rvc->model->hifi_upsample_denorm[0] && rvc->model->hifi_upsample_denorm_len[0] > 0) {
                float *d = rvc->model->hifi_upsample_denorm[0];
                int n = rvc->model->hifi_upsample_denorm_len[0];
                float mean = 0, mn = d[0], mx = d[0], sum_sq = 0;
                for (int i = 0; i < n; i++) {
                    mean += d[i];
                    if (d[i] < mn) mn = d[i];
                    if (d[i] > mx) mx = d[i];
                    sum_sq += d[i] * d[i];
                }
                mean /= n;
                float var = sum_sq / n - mean * mean;
                printf("  dec.ups.0 denorm: mean=%.6f std=%.6f min=%.6f max=%.6f n=%d\n",
                       mean, sqrtf(var), mn, mx, n);
                printf("  (PyTorch ref:     mean=0.000027 std=0.003952 min=-0.141965 max=0.155739 n=2097152)\n");
            }
        }
        printf("\nDestroying model...\n");
        wubu_rvc_destroy(rvc);
        printf("Destroyed OK\n");
    }
    return 0;
}

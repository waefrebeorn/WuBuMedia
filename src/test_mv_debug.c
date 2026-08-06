#define _USE_MATH_DEFINES
#include "wubu_vc.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main(void) {
    setbuf(stdout, NULL);
    VCConfig cfg;
    wubu_vc_default_config(&cfg);
    printf("step1: create vc\n");
    WuBuVoiceChanger *vc = wubu_vc_create(&cfg);
    printf("step2: vc=%p\n", (void*)vc);
    if (!vc) return 1;

    int n_samples = 22050 / 50;
    float *input = (float *)malloc(n_samples * sizeof(float));
    float *output = (float *)malloc(n_samples * sizeof(float));

    for (int i = 0; i < n_samples; i++) {
        input[i] = sinf(2.0f * 3.14159265f * 220.0f * i / 22050.0f) * 0.3f;
    }

    printf("step3: set_voice cartman\n");
    wubu_vc_set_voice(vc, "cartman");
    printf("step4: process_mic\n");
    int n_out = wubu_vc_process_mic(vc, input, n_samples, output, n_samples);
    printf("step5: n_out=%d\n", n_out);

    free(input); free(output);
    wubu_vc_destroy(vc);
    printf("Done\n");
    return 0;
}

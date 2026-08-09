#include "wubu_rvc_real.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

void conv1d_c(const float *in, int in_ch, int n,
              const float *w, const float *b,
              int out_ch, int k, int stride, int pad, int dil,
              float *out);

static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }

int main(int argc, char **argv) {
    int n = 120000, in_ch = 32, out_ch = 32, k = 3, pad = 1;
    float *in = (float *)malloc((size_t)in_ch * n * sizeof(float));
    float *w = (float *)malloc((size_t)out_ch * in_ch * k * sizeof(float));
    float *b = (float *)malloc((size_t)out_ch * sizeof(float));
    float *out = (float *)malloc((size_t)out_ch * n * sizeof(float));
    srand(7);
    for (int i = 0; i < in_ch * n; i++) in[i] = sinf((float)i) * 0.1f;
    for (int i = 0; i < out_ch * in_ch * k; i++) w[i] = 0.01f * (i % 3);
    for (int i = 0; i < out_ch; i++) b[i] = 0.01f;
    double t0 = now();
    for (int rep = 0; rep < 1; rep++)
        conv1d_c(in, in_ch, n, w, b, out_ch, k, 1, pad, 1, out);
    printf("n=%d k=%d 3x conv: %.3f s (%.1f ms each)\n", n, k, now() - t0, (now() - t0) * 1000 / 3);
    printf("out[0..3]=%.3f %.3f %.3f %.3f\n", out[0], out[1], out[2], out[3]);
    free(in); free(w); free(b); free(out);
    return 0;
}

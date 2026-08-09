/* header omitted: only conv1d_c is used */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

float wubu_fastexp(float x) __attribute__((weak));
float wubu_fastexp(float x) { return x; }

int wubu_generator_nsf_cuda(void) { return -1; }
void *wubu_vk_create(void) __attribute__((weak));
void *wubu_vk_create(void) { return 0; }
int wubu_vk_generator_nsf(void) __attribute__((weak));
int wubu_vk_generator_nsf(void) { return -1; }

int wubu_rvc_find_tensor(void) { return 0; }
void conv1d_c(const float *in, int in_ch, int n,
              const float *w, const float *b,
              int out_ch, int k, int stride, int pad, int dil,
              float *out);

/* scalar reference */
static void conv_ref(const float *in, int in_ch, int n,
                     const float *w, const float *b,
                     int out_ch, int k, int stride, int pad, int dil,
                     float *out) {
    int n_out = (n + 2 * pad - dil * (k - 1) - 1) / stride + 1;
    for (int oc = 0; oc < out_ch; oc++) {
        for (int j = 0; j < n_out; j++) {
            float acc = b ? b[oc] : 0.0f;
            for (int ic = 0; ic < in_ch; ic++) {
                for (int tap = 0; tap < k; tap++) {
                    int src = j * stride + tap * dil - pad;
                    if (src >= 0 && src < n)
                        acc += in[(size_t)ic * n + src] * w[((size_t)oc * in_ch + ic) * k + tap];
                }
            }
            out[(size_t)oc * n_out + j] = acc;
        }
    }
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 798;
    int in_ch = 192, out_ch = 192, k = 3, pad = 1, dil = 1;
    float *in = (float *)malloc((size_t)in_ch * n * sizeof(float));
    float *w = (float *)malloc((size_t)out_ch * in_ch * k * sizeof(float));
    float *b = (float *)malloc((size_t)out_ch * sizeof(float));
    float *out = (float *)malloc((size_t)out_ch * n * sizeof(float));
    float *ref = (float *)malloc((size_t)out_ch * n * sizeof(float));
    srand(7);
    for (int i = 0; i < in_ch * n; i++) in[i] = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
    for (int i = 0; i < out_ch * in_ch * k; i++) w[i] = ((float)rand() / RAND_MAX - 0.5f) * 0.02f;
    for (int i = 0; i < out_ch; i++) b[i] = 0.01f;
    conv1d_c(in, in_ch, n, w, b, out_ch, k, 1, pad, dil, out);
    conv_ref(in, in_ch, n, w, b, out_ch, k, 1, pad, dil, ref);
    /* compare: interior + boundaries */
    double maxdiff = 0, sum = 0;
    int nbad = 0;
    for (int i = 0; i < out_ch * n; i++) {
        double d = fabs((double)out[i] - ref[i]);
        if (d > maxdiff) maxdiff = d;
        sum += d;
        if (d > 1e-3) nbad++;
    }
    printf("n=%d k=%d maxdiff=%.6f meandiff=%.6f nbad=%d (of %d)\n", n, k, maxdiff, sum / (out_ch * n), nbad, out_ch * n);
    printf("out[0..3]=%.4f %.4f %.4f %.4f  ref[0..3]=%.4f %.4f %.4f %.4f\n",
           out[0], out[1], out[2], out[3], ref[0], ref[1], ref[2], ref[3]);
    free(in); free(w); free(b); free(out); free(ref);
    return 0;
}

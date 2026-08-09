#include "wubu_rvc_real.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

void conv1d_c(const float *in, int in_ch, int n,
              const float *w, const float *b,
              int out_ch, int k, int stride, int pad, int dil,
              float *out);

/* the transposed conv is static — replicate the reference here */
static void convt_ref(const float *in, int in_ch, int n,
                      const float *w, const float *b,
                      int out_ch, int k, int stride, int pad,
                      float *out) {
    int n_out = (n - 1) * stride - 2 * pad + k;
    memset(out, 0, (size_t)out_ch * n_out * sizeof(float));
    for (int oc = 0; oc < out_ch; oc++) {
        float *orow = out + (size_t)oc * n_out;
        for (int ic = 0; ic < in_ch; ic++) {
            const float *irow = in + (size_t)ic * n;
            const float *wk = w + ((size_t)ic * out_ch + (size_t)oc) * k;
            for (int i = 0; i < n; i++) {
                float inp = irow[i];
                int j0 = i * stride - pad;
                for (int tap = 0; tap < k; tap++) {
                    int j = j0 + tap;
                    if (j >= 0 && j < n_out) orow[j] += inp * wk[tap];
                }
            }
        }
    }
    if (b)
        for (int oc = 0; oc < out_ch; oc++)
            for (int j = 0; j < n_out; j++) out[(size_t)oc * n_out + j] += b[oc];
}

static void convt_polyphase(const float *in, int in_ch, int n,
                               const float *w, const float *b,
                               int out_ch, int k, int stride, int pad,
                               float *out) {
    int n_out = (n - 1) * stride - 2 * pad + k;
    if (n_out <= 0) return;
    memset(out, 0, (size_t)out_ch * n_out * sizeof(float));
#pragma omp parallel for schedule(static) if(out_ch >= 16 && n >= 512)
    for (int oc = 0; oc < out_ch; oc++) {
        float *orow = out + (size_t)oc * n_out;
        float bias = b ? b[oc] : 0.0f;
        for (int j = 0; j < n_out; j++) orow[j] += bias;
        for (int ic = 0; ic < in_ch; ic++) {
            const float *irow = in + (size_t)ic * n;
            if (!w) continue;
            const float *wk = w + ((size_t)ic * out_ch + (size_t)oc) * k;
            for (int p = 0; p < stride; p++) {
                int m_max = (n_out - 1 - p) / stride + 1;
                for (int tap = (p + pad) % stride; tap < k; tap += stride) {
                    int s0 = (p + pad - tap) / stride;   /* src = m + s0 */
                    float wt = wk[tap];
                    int m_lo = s0 < 0 ? -s0 : 0;
                    int m_hi = n - s0 < m_max ? n - s0 : m_max;
                    for (int m = m_lo; m < m_hi; m++)
                        orow[p + (size_t)m * stride] += irow[m + s0] * wt;
                }
            }
        }
    }
}

int main(void) {
    srand(11);
    int in_ch = 64, out_ch = 32, k = 16, stride = 10, pad = 3;
    int n = 3700;
    int n_out = (n - 1) * stride - 2 * pad + k;
    float *in = (float *)malloc((size_t)in_ch * n * sizeof(float));
    float *w = (float *)malloc((size_t)in_ch * out_ch * k * sizeof(float));
    float *b = (float *)malloc((size_t)out_ch * sizeof(float));
    float *out = (float *)malloc((size_t)out_ch * n_out * sizeof(float));
    float *ref = (float *)malloc((size_t)out_ch * n_out * sizeof(float));
    for (int i = 0; i < in_ch * n; i++) in[i] = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
    for (int i = 0; i < in_ch * out_ch * k; i++) w[i] = ((float)rand() / RAND_MAX - 0.5f) * 0.02f;
    for (int i = 0; i < out_ch; i++) b[i] = 0.01f;
    /* need the real.c's conv_transpose1d — it's static; exercise via the
     * generator? Instead: replicate with the exported conv1d_c? No — the
     * polyphase is inside real.c; compile with a macro export instead. */
    /* We can't call the static convt directly here — use the debug export. */
    convt_polyphase(in, in_ch, n, w, b, out_ch, k, stride, pad, out);
    convt_ref(in, in_ch, n, w, b, out_ch, k, stride, pad, ref);
    double maxdiff = 0; int nbad = 0;
    for (int i = 0; i < out_ch * n_out; i++) {
        double d = fabs((double)out[i] - ref[i]);
        if (d > maxdiff) maxdiff = d;
        if (d > 1e-4) nbad++;
    }
    printf("convt n=%d stride=%d maxdiff=%.6f nbad=%d (of %d)\n", n, stride, maxdiff, nbad, out_ch * n_out);
    free(in); free(w); free(b); free(out); free(ref);
    return 0;
}

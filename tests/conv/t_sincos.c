#include "wubu_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

static double now_s(void) { return (double)clock() / CLOCKS_PER_SEC; }

int main(void) {
    const int N = 1000000;
    float *x = (float *)malloc(sizeof(float) * N);
    float *s = (float *)malloc(sizeof(float) * N);
    float *c = (float *)malloc(sizeof(float) * N);
    srand(42);
    for (int i = 0; i < N; i++)
        x[i] = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 100.0f * 3.14159265f;

    /* accuracy vs libm over ±100*pi */
    double max_es = 0, max_ec = 0;
    for (int i = 0; i < 200000; i++) {
        float rs, rc;
        wubu_sincos_folded(x[i], &rs, &rc);
        float es = fabsf(rs - sinf(x[i]));
        float ec = fabsf(rc - cosf(x[i]));
        if (es > max_es) max_es = es;
        if (ec > max_ec) max_ec = ec;
    }
    printf("folded sin/cos max abs err vs libm over [-100pi,100pi]: sin=%.3e cos=%.3e\n", max_es, max_ec);

    /* speed: scalar folded vs libm sinf */
    double t0 = now_s();
    for (int i = 0; i < N; i++) s[i] = wubu_sinf_folded(x[i]);
    double t1 = now_s();
    for (int i = 0; i < N; i++) s[i] = sinf(x[i]);
    double t2 = now_s();
    printf("scalar: folded=%.1f ms  libm=%.1f ms  speedup=%.2fx\n",
           (t1 - t0) * 1000, (t2 - t1) * 1000, (t2 - t1) / (t1 - t0));

#if defined(__AVX2__) && defined(__FMA__)
    /* speed: AVX2 folded pair vs libm sinf+cosf */
    double t3 = now_s();
    for (int i = 0; i < N; i += 8) wubu_sincos8_folded(x + i, s + i, c + i, 8);
    double t4 = now_s();
    for (int i = 0; i < N; i++) { s[i] = sinf(x[i]); c[i] = cosf(x[i]); }
    double t5 = now_s();
    printf("avx2 pair: folded=%.1f ms  libm pair=%.1f ms  speedup=%.2fx\n",
           (t4 - t3) * 1000, (t5 - t4) * 1000, (t5 - t4) / (t4 - t3));
    /* AVX2 accuracy */
    double max_es8 = 0;
    for (int i = 0; i < N; i += 8) {
        wubu_sincos8_folded(x + i, s + i, c + i, 8);
        for (int j = 0; j < 8; j++) {
            float es = fabsf(s[i + j] - sinf(x[i + j]));
            if (es > max_es8) max_es8 = es;
        }
    }
    printf("avx2 folded sin max abs err vs libm: %.3e\n", max_es8);
#endif
    free(x); free(s); free(c);
    return 0;
}

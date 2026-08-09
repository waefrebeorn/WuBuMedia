/* test_load_models.c — verify BOTH checkpoint styles load through the
 * C11 weight loader (Cartman = 'weight' key; G40k = 'model' key). */
#include "wubu_rvc_parity.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    /* Real loading path: WUBU bin (from extract_rvc_weights.py) +
     * denormalization in wubu_rvc_load_weights. Cartman = 'weight' key
     * checkpoint, G40k = 'model' key checkpoint — both already extracted. */
    const char *pairs[][2] = {
        {"models/rvc/cartman/cartman_weights.bin", "cartman(v2)"},
        {"models/rvc/g40k_weights.bin", "g40k"},
    };
    int ok = 0;
    for (int i = 0; i < 2; i++) {
        WuBuRVCModel *m = (WuBuRVCModel *)calloc(1, sizeof(WuBuRVCModel));
        if (!m) continue;
        int rc = wubu_rvc_load_weights(m, pairs[i][0]);
        printf("[%s] load_weights rc=%d tensors=%d\n", pairs[i][1], rc,
               m ? m->n_tensors : -1);
        if (rc == 0) {
            ok++;
            const RVCTensor *e = wubu_rvc_find_tensor(m, "enc_p.emb_phone.weight");
            printf("  enc_p.emb_phone.weight dims=(%d,%d) -> content_dim=%d\n",
                   e ? e->dims[0] : -1, e ? e->dims[1] : -1,
                   e ? e->dims[1] : -1);
            const RVCTensor *conv_pre = wubu_rvc_find_tensor(m, "dec.conv_pre.weight");
            printf("  dec.conv_pre.weight dims=(%d,%d,%d)\n",
                   conv_pre ? conv_pre->dims[0] : -1,
                   conv_pre ? conv_pre->dims[1] : -1,
                   conv_pre ? conv_pre->dims[2] : -1);
        }
        wubu_rvc_model_free(m);  /* frees model struct itself too */
    }
    printf(ok == 2 ? "LOAD-ALL: PASS\n" : "LOAD-ALL: FAIL (%d/2)\n", ok);
    return ok == 2 ? 0 : 1;
}

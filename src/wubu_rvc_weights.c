/* wubu_rvc_weights.c — Flat-binary .pth weight loader for WuBuRVC.
 *
 * Reads the WUBU binary format produced by tools/extract_rvc_weights.py:
 *   [4B] magic "WUBU"
 *   [4B] n_tensors (uint32)
 *   per tensor: [1B name_len] [name] [4B n_dims] [dims...] [4B data_len] [data]
 *
 * License: WaefreBeorn-UMV3
 */
#define _USE_MATH_DEFINES
#include "wubu_rvc_parity.h"
#include "wubu_rvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Load a WUBU-format binary weight file and map tensors into the model.
 * Returns 0 on success, -1 on failure. */
int wubu_rvc_load_weights(WuBuRVCModel *model, const char *bin_path) {
    if (!model || !bin_path) return -1;

    FILE *f = fopen(bin_path, "rb");
    if (!f) {
        fprintf(stderr, "WuBuRVC: cannot open weight file %s\n", bin_path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    /* Verify magic */
    if (memcmp(buf, "WUBU", 4) != 0) {
        free(buf);
        fprintf(stderr, "WuBuRVC: bad magic in %s (not a WUBU weight file)\n", bin_path);
        return -1;
    }

    uint32_t n_tensors = read_u32(buf + 4);

    /* Allocate tensor map */
    model->tensors = (RVCTensor *)calloc((size_t)n_tensors, sizeof(RVCTensor));
    model->n_tensors = (int)n_tensors;
    if (!model->tensors) { free(buf); return -1; }

    size_t off = 8;
    int stored = 0;  /* index into model->tensors[] (separate from loop counter) */

    for (uint32_t t = 0; t < n_tensors && off < (size_t)fsize; t++) {
        uint8_t name_len = buf[off]; off += 1;
        if (off + name_len > (size_t)fsize) break;

        char name[256];
        memcpy(name, buf + off, name_len);
        name[name_len < 255 ? name_len : 255] = '\0';
        off += name_len;

        uint32_t n_dims = read_u32(buf + off); off += 4;
        if (off + n_dims * 4 > (size_t)fsize) break;

        int dims[4] = {0,0,0,0};
        uint32_t total = 1;
        for (uint32_t d = 0; d < n_dims; d++) {
            dims[d] = (int)read_u32(buf + off); off += 4;
            total *= dims[d];
        }

        uint32_t data_len = read_u32(buf + off); off += 4;
        if (off + data_len > (size_t)fsize) break;

        /* Allocate + copy tensor data */
        float *data = (float *)malloc((size_t)data_len);
        if (!data) { off += data_len; continue; }
        memcpy(data, buf + off, data_len);
        off += data_len;

        float *wp = data;

        /* Map dec.ups.N.weight_v → hifi_upsample[N] (the actual conv weight) */
        if (strstr(name, "dec.ups.") && strstr(name, "weight_v")) {
            for (int i = 0; i < 4; i++) {
                char pat[64];
                snprintf(pat, sizeof(pat), "dec.ups.%d.weight_v", i);
                if (strstr(name, pat) != NULL) {
                    model->hifi_upsample[i] = wp;
                    model->hifi_upsample_len[i] = (int)total;
                    break;
                }
            }
        }
        if (strstr(name, "dec.resblocks.0.convs1.0.weight_v")) {
            if (model->hifi_mrf0 == NULL) {
                model->hifi_mrf0 = wp;
                model->hifi_mrf0_len = (int)total;
            }
        }
        if (strstr(name, "dec.resblocks.0.convs2.0.weight_v")) {
            if (model->hifi_mrf1 == NULL) {
                model->hifi_mrf1 = wp;
                model->hifi_mrf1_len = (int)total;
            }
        }
        if (strstr(name, "noise_convs.0.weight")) {
            if (model->vocoder_conv_pre == NULL) {
                model->vocoder_conv_pre = wp;
                model->vocoder_conv_pre_len = (int)total;
            }
        }
        if (strstr(name, "dec.conv_post.weight")) {
            if (model->vocoder_conv_post == NULL) {
                model->vocoder_conv_post = wp;
                model->vocoder_conv_post_len = (int)total;
            }
        }

        /* Store tensor in lookup map (uses stored as index, not loaded) */
        if ((uint32_t)stored < n_tensors) {
            strncpy(model->tensors[stored].name, name,
                    sizeof(model->tensors[0].name) - 1);
            model->tensors[stored].data = wp;
            model->tensors[stored].n_dims = (int)n_dims;
            for (int d = 0; d < 4; d++) model->tensors[stored].dims[d] = dims[d];
            stored++;
        }
    }

    /* Post-load: de-normalize weight_norm tensors (weight_g * weight_v/||v||).
     * Cartman v2 HiFi-GAN has 4 upsampling convs + 18 MRF resblock convs
     * (3 stacks × 3 blocks × 2 conv types = 18 convs, each with weight_g + weight_v). */

    /* Upsampling layers: dec.ups.N.weight_g (N,1,1) + weight_v (N, in_ch, k) */
    for (int i = 0; i < 4; i++) {
        if (model->hifi_upsample[i] && model->hifi_upsample_len[i] > 0) {
            char g_name[128];
            snprintf(g_name, sizeof(g_name), "dec.ups.%d.weight_g", i);
            const RVCTensor *wg_t = wubu_rvc_find_tensor(model, g_name);
            if (wg_t && wg_t->data) {
                int n_elems = model->hifi_upsample_len[i];
                int n_ch = wg_t->dims[0];  /* weight_g: (n_out, 1, 1) → n_out channels */
                model->hifi_upsample_denorm[i] = (float *)malloc((size_t)n_elems * sizeof(float));
                if (model->hifi_upsample_denorm[i]) {
                    wubu_rvc_denormalize_weight(wg_t->data, model->hifi_upsample[i],
                                                model->hifi_upsample_denorm[i],
                                                n_elems, n_ch);
                    model->hifi_upsample_denorm_len[i] = n_elems;
                }
            }
        }
    }

    for (int s = 0; s < 12; s++) {
        for (int k_idx = 0; k_idx < 2; k_idx++) {
            const char *conv_name = k_idx == 0 ? "convs1" : "convs2";
            for (int b = 0; b < 3; b++) {
                char key[128];
                snprintf(key, sizeof(key), "dec.resblocks.%d.%s.%d.weight_v", s, conv_name, b);
                RVCTensor *wv_t = (RVCTensor *)wubu_rvc_find_tensor(model, key);
                if (wv_t && wv_t->data) {
                    char gkey[128];
                    snprintf(gkey, sizeof(gkey), "dec.resblocks.%d.%s.%d.weight_g", s, conv_name, b);
                    const RVCTensor *wg_t = wubu_rvc_find_tensor(model, gkey);
                    if (wg_t && wg_t->data) {
                        /* De-normalize in place: weight_v gets replaced with weight_g*weight_v/||v|| */
                        int n_elems = (int)(wv_t->dims[0] * wv_t->dims[1] * wv_t->dims[2]);
                        int n_ch = wg_t->dims[0];  /* 256 */
                        float *denorm = (float *)malloc((size_t)n_elems * sizeof(float));
                        if (denorm) {
                            wubu_rvc_denormalize_weight(wg_t->data, wv_t->data,
                                                        denorm, n_elems, n_ch);
                            free(wv_t->data);
                            wv_t->data = denorm;
                        }
                    }
                }
            }
        }
    }

    /* Set model params.
     * RVC v2 config: config[16] = hidden_channels (256), config[17] = sample_rate (40000)
     * We don't store config in WUBU binary — use tensor-shape inference + known values. */
    model->loaded = 1;
    model->in_memory = 1;
    /* Hidden channels: 256 for RVC v2 (dec.conv_pre out = hidden*2 for PixelShuffle).
     * Cartman has dec.conv_pre.weight: (512, 192, 7) -> 512/2 = 256. */
    if (model->hidden_channels == 0) model->hidden_channels = 256;
    /* Sample rate: Cartman config[17] = 40000 (40k). Set if not already configured. */
    if (model->sample_rate == 0 || model->sample_rate == 22050) model->sample_rate = 40000;

    fprintf(stderr, "WuBuRVC: loaded %d/%u tensors from %s (%zu bytes)\n",
            stored, n_tensors, bin_path, (size_t)fsize);

    free(buf);
    return 0;
}

/* Lookup a tensor by exact name in the loaded model. Returns NULL if not found.
 * Uses exact strcmp only — substring matching is unsafe (e.g.,
 * "dec.resblocks.0.convs1.0.weight_v" would match "dec.resblocks.0.convs1.0.weight_v.bias"). */
const RVCTensor *wubu_rvc_find_tensor(const WuBuRVCModel *model,
                                       const char *name) {
    if (!model || !name || !model->tensors) return NULL;
    for (int i = 0; i < model->n_tensors; i++) {
        if (strcmp(model->tensors[i].name, name) == 0) {
            return &model->tensors[i];
        }
    }
    return NULL;
}

/* Apply weight normalization de-normalization: W = weight_g * (weight_v / ||weight_v||)
 * Resolves weight_g + weight_v decomposition used in RVC v2 checkpoints.
 * Output is written to 'out' (caller allocates, size = n_elements). */
void wubu_rvc_denormalize_weight(const float *weight_g, const float *weight_v,
                                  float *out, int n_elements, int n_channels) {
    if (!weight_g || !weight_v || !out || n_elements <= 0 || n_channels <= 0) return;
    for (int ch = 0; ch < n_channels; ch++) {
        float norm_sq = 0.0f;
        int per_ch = n_elements / n_channels;
        const float *v_ch = weight_v + (size_t)ch * per_ch;
        for (int i = 0; i < per_ch; i++) {
            norm_sq += v_ch[i] * v_ch[i];
        }
        float norm = sqrtf(norm_sq) + 1e-8f;
        float g = weight_g[ch];
        float scale = g / norm;
        for (int i = 0; i < per_ch; i++) {
            out[(size_t)ch * per_ch + i] = v_ch[i] * scale;
        }
    }
}

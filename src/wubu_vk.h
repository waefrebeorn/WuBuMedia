/* wubu_vk.h — Vulkan compute accelerator for the WuBuMedia engine.
 *
 * Cross-vendor GPU kernels (NVIDIA/AMD/Intel) via the Vulkan compute API —
 * a C-native alternative to CUDA that needs no vendor SDK. Same math as the
 * CPU kernels (wubu_rvc_real.c conv1d_c) and the CUDA kernels
 * (wubu_rvc_cuda.cu k_conv1d).
 *
 * Opaque struct; no god header. C11 only.
 *
 * License: WaefreBeorn-UMV3
 */
#ifndef WUBU_VK_H
#define WUBU_VK_H

typedef struct WuBuVk WuBuVk;   /* opaque */

/* Create the Vulkan context: instance, first compute-capable physical
 * device, device + compute queue, the conv1d pipeline (SPIR-V embedded),
 * and a growable storage-buffer pool. Returns NULL on failure. */
WuBuVk *wubu_vk_create(void);

/* Tear everything down. */
void wubu_vk_destroy(WuBuVk *vk);

/* conv1d — identical signature/behavior to wubu_rvc_real.c conv1d_c:
 *   in   [in_ch * n]      (col-major: channel-major, n contiguous)
 *   w    [out_ch * in_ch * k]
 *   b    [out_ch] or NULL
 *   out  [out_ch * n_out]
 * n_out = (n + 2*pad - dil*(k-1) - 1)/stride + 1.
 * The buffers are reallocated lazily (host-visible); uploads and the
 * readback are synchronous. Returns 0 on success, -1 on failure. */
int wubu_vk_conv1d(WuBuVk *vk,
                   const float *in, int in_ch, int n,
                   const float *w, const float *b,
                   int out_ch, int k, int stride, int pad, int dil,
                   float *out, int n_out);

#endif /* WUBU_VK_H */

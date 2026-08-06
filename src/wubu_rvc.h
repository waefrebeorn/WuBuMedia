#ifndef WUBU_RVC_H
#define WUBU_RVC_H

/* wubu_rvc.h — WuBuRVC: Our own RVC inference engine (C11).
 *
 * Key insight: We don't need RVC's legacy pipeline. We load existing .pth
 * model weights (Hubert content encoder + flow + HiFi-GAN generator/vocoder),
 * extract the tensor values, and reinterpret them in OUR OWN virtualized
 * frame buffer space. Then we execute through our own fused kernels —
 * designed like a game engine with a unified buffer abstraction that works
 * on both CPU and GPU.
 *
 * Architecture:
 *
 *   .pth/.onnx/.index files ──→ wubu_rvc_load_model()
 *       │  (gguf_reader extracts tensor names + data)
 *       ▼
 *   RVCGraph { tensor_map: name → tensor_view }
 *       │  (we map RVC's layer structure into our own IR)
 *       ▼
 *   wubu_frame_buffer_t — virtualized frame buffer space
 *       │  (unified CPU/GPU memory with layout abstraction)
 *       ▼
 *   Fused kernels (our design):
 *     • wubu_kernel_autoname()   — ActNorm as inline buffer op
 *     • wubu_kernel_flow_couple() — Affine Coupling fused into 1 pass
 *     • wubu_kernel_hifigan()    — Upsample + MRF + LRELU fused
 *     • wubu_kernel_vocoder()    — Residual stack + tanh fused
 *       │
 *       ▼
 *   Output waveform  ←  wubu_frame_buffer_read()
 *
 * License: WaefreBeorn-UMV3
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RVC model format version */
typedef enum {
    RVC_V1 = 1,
    RVC_V2 = 2,
    RVC_V3 = 3
} RVCVersion;

/* ---- Error codes (visible in both C and C++) ---- */
#define WUBU_RVC_OK           0
#define WUBU_RVC_ERR_NOGPU    -1
#define WUBU_RVC_ERR_MODEL    -2
#define WUBU_RVC_ERR_NOINIT   -3
#define WUBU_RVC_ERR_ARGS     -4
#define WUBU_RVC_ERR_CUDA     -5
#define WUBU_RVC_ERR_FILE     -6

/* ---- Virtualized Frame Buffer ---- */
/* Unified abstraction: a named region of memory that can live on CPU or GPU.
 * We design it like a game engine's render target — you bind buffers,
 * attach them to kernels, and the engine handles the transfer. */

typedef enum {
    WUBU_BUF_CPU = 0,
    WUBU_BUF_CUDA = 1,
    WUBU_BUF_MANAGED = 2
} WuBuBufferType;

typedef struct {
    void       *ptr;        /* CPU or GPU pointer */
    size_t      bytes;      /* allocated size */
    WuBuBufferType type;
    int         device_id;  /* GPU device (if CUDA) */
    char        name[64];   /* debug label */
} wubu_frame_buffer_t;

/* Create a frame buffer (allocates CPU or GPU memory) */
int wubu_frame_buffer_create(wubu_frame_buffer_t *fb, size_t n_floats,
                              WuBuBufferType type, const char *name);

/* Destroy a frame buffer */
void wubu_frame_buffer_destroy(wubu_frame_buffer_t *fb);

/* Copy data in/out (handles CPU↔GPU automatically) */
int wubu_frame_buffer_write(wubu_frame_buffer_t *fb,
                             const float *src, size_t n_floats);
int wubu_frame_buffer_read(const wubu_frame_buffer_t *fb,
                            float *dst, size_t n_floats);

/* ---- RVC Model IR (Intermediate Representation) ---- */
/* After loading a .pth, we build a graph that maps tensor names
 * to our own execution order. This is what makes us "not stuck
 * by old standards" — we reinterpret the model structure. */

typedef struct {
    char   name[128];    /* tensor name from .pth (e.g. "generator.ups.0.weight") */
    float *data;          /* tensor data (CPU pointer) */
    int    n_dims;       /* number of dimensions */
    int    dims[4];      /* dimension sizes (max 4D for RVC) */
    int    offset;       /* byte offset in the loaded weight blob */
} RVCTensor;

typedef struct {
    RVCTensor *tensors;     /* all weight tensors */
    int        n_tensors;
    int        version;     /* RVC_V1, V2, V3 */
    int        sample_rate;
    int        mel_channels;
    int        hidden_channels;
    int        n_flow_layers;
    int        n_upsample_layers;
    int        n_mrf_stacks;
    int        n_residual_layers;
} RVCGraph;

/* ---- Main RVC engine ---- */
typedef struct WuBuRVC WuBuRVC;

typedef struct {
    char  model_path[512];
    char  index_path[512];
    char  hubert_path[512];
    RVCVersion version;
    int   sample_rate;
    int   use_cuda;
    int   gpu_device;
    int   fp16;
    int   mel_channels;
    int   hidden_channels;
    int   filter_length;
    int   hop_length;
    int   win_length;
    int   n_flow_layers;
    int   n_hifigan_upsamples;
    int   n_mrf_stacks;
    int   n_residual_layers;
    int   reservoir_size;
    double speed_factor;
    double pitch_shift;
} RVCConfig;

/* Engine info */
typedef struct {
    int   cuda_available;
    int   cuda_device_count;
    char  cuda_device_name[256];
    int   cuda_major, cuda_minor;
    size_t vram_total_mb;
    size_t vram_used_mb;
    int   rvc_version;
    long  total_inferences;
    long  cache_hits;
    double last_latency_ms;
} RVCInfo;

/* Load model weights and build RVCGraph IR.
 * This is the ONLY thing we need from the legacy pipeline —
 * we load and reinterpret everything else ourselves. */
WuBuRVC *wubu_rvc_load(const RVCConfig *cfg);
void     wubu_rvc_destroy(WuBuRVC *rvc);

/* Synthesize waveform from mel-spectrogram.
 * Uses our fused kernels through the frame buffer abstraction. */
int wubu_rvc_synthesize(WuBuRVC *rvc,
                         const float *mel_input, int n_frames, int mel_ch,
                         float *output, int n_samples);

/* Convert raw audio directly (mel extraction internal). */
int wubu_rvc_convert_audio(WuBuRVC *rvc,
                            const float *input, int n_input,
                            float *output, int n_samples);

/* Get info */
void wubu_rvc_info(const WuBuRVC *rvc, RVCInfo *out);

/* ---- Kernel launchers (our own fused kernels) ---- */
/* These operate on wubu_frame_buffer_t, not raw pointers.
 * This is our "game engine" abstraction — everything goes through
 * the frame buffer, and we decide at runtime whether it's on CPU or GPU. */

/* Fused ActNorm: normalize buffer in-place */
int wubu_kernel_autonorm(wubu_frame_buffer_t *fb,
                          const float *scale, const float *bias,
                          int n_channels);

/* Fused Affine Coupling: split + transform + swap */
int wubu_kernel_flow_couple(wubu_frame_buffer_t *input,
                             wubu_frame_buffer_t *output,
                             const float *coupling_w,
                             const float *coupling_b,
                             int n_frames, int hidden_ch);

/* Fused HiFi-GAN: Upsample + MRF + LRELU */
int wubu_kernel_hifigan(wubu_frame_buffer_t *input,
                         wubu_frame_buffer_t *output,
                         const float *upsample_w,
                         const float *upsample_b,
                         const float *mrf_w,
                         int n_input, int n_output, int hidden_ch);

/* Fused vocoder: Residual stack + tanh */
int wubu_kernel_vocoder(wubu_frame_buffer_t *input,
                         wubu_frame_buffer_t *output,
                         const float *res_w, const float *res_b,
                         const float *out_w,
                         int n_samples, int n_layers);

/* Sync frame buffer (CPU↔GPU transfer) */
int wubu_frame_buffer_sync(wubu_frame_buffer_t *fb);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_RVC_H */

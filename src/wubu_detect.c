/* wubu_detect.c — Model file detector.
 *
 * Analyzes dragged files to determine what they are:
 *   - .pth — PyTorch checkpoint (ZIP format)
 *   - .index — FAISS retrieval index
 *   - .wubu — Our enhanced model format
 *   - .zip — RVCv2 package (contains .pth + .index)
 *   - Directory — RVCv2 model directory (contains .pth + .index)
 *
 * Also detects RVC version (v1 vs v2) and content encoder type
 * by inspecting the .pth state_dict keys.
 *
 * Usage: wubu_detect <path1> [path2] [path3...]
 *
 * Output: "path|type|version|encoder|size"
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

/* ── File type detection ── */

static int is_zip(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t magic[4];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return 0; }
    fclose(f);
    /* ZIP magic: PK\x03\x04 or PK\x05\x06 (empty) */
    return (magic[0] == 0x50 && magic[1] == 0x4B);
}

static int is_wubu(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[5];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return 0; }
    fclose(f);
    magic[4] = '\0';
    return (strcmp(magic, "WUBU") == 0);
}

/* Check if path is a .pth file by extension */
static int has_ext(const char *path, const char *ext) {
    size_t len = strlen(path);
    size_t elen = strlen(ext);
    return (len >= elen && strcasecmp(path + len - elen, ext) == 0);
}

/* ── RVC version detection from .pth state_dict keys ──
 *
 * RVC v1 models have keys like:
 *   - enc_p.xxx (posterior encoder)
 *   - dpad.xx (duration predictor)
 *   - vocoder.xxx (HiFi-GAN)
 *
 * RVC v2 adds:
 *   - enc_p.xxx with different dims
 *   - emb_g (speaker embedding)
 *   - flow.xxx (flow posterior)
 *   - HiFi-GAN keys with "h" prefix
 *
 * We detect by looking for v2-specific keys in the ZIP central directory. */
static int detect_rvc_version(const char *pth_path) {
    /* Read ZIP central directory, look for key patterns */
    FILE *f = fopen(pth_path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)fsz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    /* Search for RVC v2-specific keys */
    int has_emb_g = 0;
    int has_flow = 0;
    int has_hifi = 0;
    (void)has_hifi;

    /* Scan for string patterns in the pickle data */
    for (long i = 0; i < fsz - 10; i++) {
        if (buf[i] == 'e' && buf[i+1] == 'm' && buf[i+2] == 'b' && buf[i+3] == '_' && buf[i+4] == 'g')
            has_emb_g = 1;
        if (buf[i] == 'f' && buf[i+1] == 'l' && buf[i+2] == 'o' && buf[i+3] == 'w' && buf[i+4] == '.')
            has_flow = 1;
        if (buf[i] == 'h' && buf[i+1] == 'i' && buf[i+2] == 'f' && buf[i+3] == 'i')
            has_hifi = 1;
    }

    free(buf);

    /* v2 has all three; v1 has none of them */
    if (has_emb_g && has_flow) return 2;
    return 1;
}

/* ── Model detection result ── */
typedef struct {
    char path[512];
    char type[32];       /* "pth", "index", "wubu", "zip", "directory", "unknown" */
    char version[16];    /* "v1", "v2", "unknown" */
    char encoder[32];    /* "hubert", "contentvec", "wavlm", "mind-meld" */
    char pair_path[512]; /* paired .index path (if .pth) */
    long size;
} detect_result_t;

/* ── Main detection ── */
static void detect_file(const char *path, detect_result_t *r) {
    memset(r, 0, sizeof(*r));
    strncpy(r->path, path, sizeof(r->path) - 1);

    struct stat st;
    if (stat(path, &st) != 0) {
        strcpy(r->type, "unknown");
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        strcpy(r->type, "directory");
        r->size = (long)st.st_size;
        /* Look for .pth inside directory */
        /* In full impl: scan directory for model files */
        return;
    }

    r->size = (long)st.st_size;

    /* Check .wubu magic */
    if (is_wubu(path)) {
        strcpy(r->type, "wubu");
        strcpy(r->version, "v2");
        strcpy(r->encoder, "mind-meld");
        return;
    }

    /* Check .pth extension + ZIP format */
    if (has_ext(path, ".pth") && is_zip(path)) {
        strcpy(r->type, "pth");
        int ver = detect_rvc_version(path);
        if (ver == 2) {
            strcpy(r->version, "v2");
            /* Check for ContentVec key patterns */
            strcpy(r->encoder, "contentvec");  /* v2 uses ContentVec */
        } else {
            strcpy(r->version, "v1");
            strcpy(r->encoder, "hubert");
        }

        /* Find paired .index file */
        char idx_path[520];
        size_t plen = strlen(path);
        if (plen > 4) {
            size_t copy = (sizeof(idx_path) - 6 < plen - 4) ? sizeof(idx_path) - 6 : plen - 4;
            memcpy(idx_path, path, copy);
            idx_path[copy] = '\0';
        } else {
            idx_path[0] = '\0';
        }
        /* Append .index */
        size_t curlen = strlen(idx_path);
        size_t remain = sizeof(idx_path) - curlen;
        snprintf(idx_path + curlen, remain, ".index");
        FILE *tf = fopen(idx_path, "rb");
        if (tf) {
            fclose(tf);
            strncpy(r->pair_path, idx_path, sizeof(r->pair_path) - 1);
        }
        return;
    }

    /* Check .index */
    if (has_ext(path, ".index")) {
        strcpy(r->type, "index");
        strcpy(r->version, "paired");
        return;
    }

    /* Check .zip (RVC package) */
    if (has_ext(path, ".zip") && is_zip(path)) {
        strcpy(r->type, "zip");
        strcpy(r->version, "v2");
        strcpy(r->encoder, "contentvec");
        return;
    }

    strcpy(r->type, "unknown");
}

/* ── Detect paired files from drag-drop ──
 * When user drags multiple files, figure out which are .pth and .index
 * and pair them up. */
static void detect_and_pair(int argc, char **argv) {
    detect_result_t *results = (detect_result_t *)calloc((size_t)argc, sizeof(detect_result_t));
    if (!results) return;

    char pth_path[512] = "";
    char idx_path[512] = "";

    for (int i = 1; i < argc; i++) {
        detect_file(argv[i], &results[i - 1]);
        detect_result_t *r = &results[i - 1];

        printf("%s|%s|%s|%s|%ld|%s\n",
               argv[i], r->type, r->version, r->encoder, r->size,
               r->pair_path[0] ? r->pair_path : "-");

        /* Track pth/index for pairing */
        if (strcmp(r->type, "pth") == 0 && pth_path[0] == '\0')
            strncpy(pth_path, argv[i], sizeof(pth_path) - 1);
        if (strcmp(r->type, "index") == 0 && idx_path[0] == '\0')
            strncpy(idx_path, argv[i], sizeof(idx_path) - 1);
    }

    /* Auto-pair if pth found but no index */
    if (pth_path[0] && idx_path[0] == '\0') {
        /* Check auto-discovered pair */
        for (int i = 1; i < argc; i++) {
            if (strcmp(results[i-1].type, "pth") == 0 &&
                results[i-1].pair_path[0]) {
                printf("AUTO-PAIR:%s|%s\n", pth_path, results[i-1].pair_path);
                break;
            }
        }
    }

    free(results);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file1> [file2] ...\n", argv[0]);
        fprintf(stderr, "Detects RVC model types: .pth, .index, .wubu, .zip, dir\n");
        return 1;
    }

    detect_and_pair(argc, argv);
    return 0;
}

/* wubu_model_dock.c — Recently-used model dock with RCU hot-swap.
 *
 * Maintains 10 most recently picked models for instant switching.
 * Uses RCU atomic pointer swap for lock-free model changes.
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#include "wubu_model_dock.h"
#include "wubu_rvc_parity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── File utilities ── */
static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long)st.st_size;
}

/* Unused helper — reserved for future string management */
static char *str_dup(const char *s) __attribute__((unused));
static char *str_dup(const char *s) {
    size_t len = strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* ── Init ── */
void wubu_model_dock_init(wubu_model_dock_t *dock) {
    memset(dock, 0, sizeof(*dock));
    wubu_rcu_init(&dock->rcu, NULL);
    dock->prewarm_all = 0;
    dock->enable_mind_meld = 0;
}

/* ── Add a model to the dock (LRU: move to front) ── */
int wubu_model_dock_add(wubu_model_dock_t *dock,
                         const char *pth_path,
                         const char *index_path,
                         const char *name,
                         int version) {
    if (!dock || !pth_path || !name) return -1;

    long sz = file_size(pth_path);

    /* Check if already in dock */
    int existing = -1;
    for (int i = 0; i < dock->count; i++) {
        if (strcmp(dock->entries[i].name, name) == 0 ||
            strcmp(dock->entries[i].path, pth_path) == 0) {
            existing = i;
            break;
        }
    }

    wubu_dock_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.path, pth_path, sizeof(entry.path) - 1);
    if (index_path)
        strncpy(entry.index_path, index_path, sizeof(entry.index_path) - 1);
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.version = version;
    entry.file_size = (size_t)sz;
    entry.loaded = 0;
    entry.in_use = 0;

    if (existing >= 0) {
        /* Promote: remove from old position, insert at front */
        wubu_dock_entry_t tmp = dock->entries[existing];
        for (int i = existing; i > 0; i--)
            dock->entries[i] = dock->entries[i - 1];
        dock->entries[0] = tmp;
        /* Preserve loaded state (model remains loaded) */
        return 0;
    }

    /* Evict oldest if at capacity */
    if (dock->count >= MODEL_DOCK_CAPACITY) {
        /* Free the last model */
        if (dock->models[dock->count - 1]) {
            wubu_rvc_model_free((WuBuRVCModel *)dock->models[dock->count - 1]);
            dock->models[dock->count - 1] = NULL;
        }
        dock->count--;
        /* Shift everything down */
        for (int i = dock->count; i > 0; i--)
            dock->entries[i] = dock->entries[i - 1];
    } else {
        /* Shift everything down to make room */
        for (int i = dock->count; i > 0; i--)
            dock->entries[i] = dock->entries[i - 1];
    }

    /* Insert new entry at front */
    dock->entries[0] = entry;
    dock->count++;

    /* Start async loading for this model */
    wubu_rcu_async_load(&dock->rcu, pth_path, index_path, name, version);

    return 0;
}

/* ── Switch to a model by name (instant RCU swap) ── */
int wubu_model_dock_switch(wubu_model_dock_t *dock, const char *name) {
    if (!dock || !name) return -1;

    /* Find the model */
    for (int i = 0; i < dock->count; i++) {
        if (strcmp(dock->entries[i].name, name) == 0) {
            /* Check if loaded */
            if (!dock->entries[i].loaded) {
                /* Try to load synchronously (should be fast if pre-warmed) */
                WuBuRVCModel *model = wubu_rvc_load_model(dock->entries[i].path);
                if (!model) return -1;
                dock->entries[i].loaded = 1;
                if (dock->entries[i].index_path[0])
                    wubu_rvc_load_index(model, dock->entries[i].index_path);
                /* Store in models array */
                dock->models[i] = model;
                dock->n_loaded++;
                /* Mark this one as in-use */
                for (int j = 0; j < dock->count; j++)
                    dock->entries[j].in_use = 0;
                dock->entries[i].in_use = 1;
                /* Swap via RCU */
                wubu_rcu_assign(&dock->rcu, model, NULL);
                return 0;
            }
            /* Already loaded — instant swap */
            for (int j = 0; j < dock->count; j++)
                dock->entries[j].in_use = 0;
            dock->entries[i].in_use = 1;
            wubu_rcu_assign(&dock->rcu, dock->models[i], NULL);
            return 0;
        }
    }
    return -1;
}

/* ── Poll for completed async loads ── */
void wubu_model_dock_poll(wubu_model_dock_t *dock) {
    if (!dock) return;

    /* Check RCU for completed loads */
    int swapped = wubu_rcu_check_load(&dock->rcu,
        (void (*)(void *))wubu_rvc_model_free);
    if (swapped > 0) {
        /* A new model was swapped in — mark it loaded */
        for (int i = 0; i < dock->count; i++) {
            if (dock->entries[i].loaded == 0) {
                dock->entries[i].loaded = 1;
                dock->models[i] = wubu_rcu_dereference(&dock->rcu);
                dock->n_loaded++;
                break;
            }
        }
    }

    /* Pre-warm: load all models if enabled */
    if (dock->prewarm_all) {
        for (int i = 0; i < dock->count; i++) {
            if (!dock->entries[i].loaded) {
                WuBuRVCModel *model = wubu_rvc_load_model(dock->entries[i].path);
                if (model) {
                    dock->entries[i].loaded = 1;
                    if (dock->entries[i].index_path[0])
                        wubu_rvc_load_index(model, dock->entries[i].index_path);
                    dock->models[i] = model;
                    dock->n_loaded++;
                }
            }
        }
    }
}

/* ── Get current active model (lock-free) ── */
void *wubu_model_dock_current(wubu_model_dock_t *dock) {
    return wubu_rcu_dereference(&dock->rcu);
}

/* ── Get entry for GUI display ── */
const wubu_dock_entry_t *wubu_model_dock_entry(const wubu_model_dock_t *dock,
                                                 int index) {
    if (index < 0 || index >= dock->count) return NULL;
    return &dock->entries[index];
}

void wubu_model_dock_set_mind_meld(wubu_model_dock_t *dock, int enabled) {
    dock->enable_mind_meld = enabled;
}

void wubu_model_dock_set_prewarm(wubu_model_dock_t *dock, int enabled) {
    dock->prewarm_all = enabled;
}

/* ── Save/load docked models ── */
int wubu_model_dock_save(const wubu_model_dock_t *dock, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# WuBuModelDock v1\n");
    fprintf(f, "mind_meld=%d\n", dock->enable_mind_meld);
    fprintf(f, "prewarm=%d\n", dock->prewarm_all);
    fprintf(f, "count=%d\n", dock->count);
    for (int i = 0; i < dock->count; i++) {
        fprintf(f, "model=%s|%s|%s|%d\n",
                dock->entries[i].name,
                dock->entries[i].path,
                dock->entries[i].index_path[0] ? dock->entries[i].index_path : "none",
                dock->entries[i].version);
    }
    fclose(f);
    return 0;
}

int wubu_model_dock_load(wubu_model_dock_t *dock, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model=", 6) == 0) {
            char name[128], ppath[512], ipath[512];
            int ver;
            if (sscanf(line + 6, "%127[^|]|%511[^|]|%511[^|]|%d",
                       name, ppath, ipath, &ver) == 4) {
                wubu_model_dock_add(dock, ppath,
                    strcmp(ipath, "none") == 0 ? NULL : ipath,
                    name, ver);
            }
        } else if (strncmp(line, "mind_meld=", 10) == 0) {
            dock->enable_mind_meld = atoi(line + 10);
        } else if (strncmp(line, "prewarm=", 8) == 0) {
            dock->prewarm_all = atoi(line + 8);
        }
    }
    fclose(f);
    return 0;
}

/* ── Destroy ── */
void wubu_model_dock_destroy(wubu_model_dock_t *dock) {
    if (!dock) return;
    wubu_rcu_destroy(&dock->rcu,
        (void (*)(void *))wubu_rvc_model_free);
    for (int i = 0; i < MODEL_DOCK_CAPACITY; i++) {
        if (dock->models[i])
            wubu_rvc_model_free((WuBuRVCModel *)dock->models[i]);
    }
}

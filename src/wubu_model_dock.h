#ifndef WUBU_MODEL_DOCK_H
#define WUBU_MODEL_DOCK_H

/* wubu_model_dock.h — Recently-used model dock with RCU hot-swap.
 *
 * Maintains the 10 most recently picked RVC models for instant
 * switching with zero load lag. Uses RCU (read-copy-update) for
 * lock-free hot-swapping: while model A serves audio, model B loads
 * in the background. Swap is a single atomic pointer exchange.
 *
 * Research basis:
 * - RVC models are 50-500MB on disk; loading takes 100-800ms
 * - RCU eliminates the "click" when switching mid-stream
 * - 10-model LRU cache fits in typical VRAM (2-8GB) when pre-warmed
 *
 * License: WaefreBeorn-UMV3
 */

#include <stddef.h>
#include "wubu_rcu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODEL_DOCK_CAPACITY 10

/* A single docked model entry */
typedef struct {
    char   path[512];           /* .pth file path */
    char   index_path[512];     /* .index file path (or empty) */
    char   name[128];           /* human-readable name */
    int    version;             /* 1 (v1) or 2 (v2) */
    int    loaded;              /* 1 = model loaded in memory */
    int    in_use;             /* 1 = currently active */
    size_t file_size;          /* bytes (for LRU eviction) */
} wubu_dock_entry_t;

/* The model dock — manages 10 most recent models + RCU hot-swap */
typedef struct {
    wubu_dock_entry_t entries[MODEL_DOCK_CAPACITY];
    int               count;
    wubu_rcu_slot     rcu;     /* RCU-protected active model pointer */
    void             *models[MODEL_DOCK_CAPACITY];  /* loaded model pointers */
    int               n_loaded;  /* number of models in memory */

    /* Pre-warmed cache for sub-10ms switching */
    int  prewarm_all;     /* 1 = keep all models in VRAM */
    int  enable_mind_meld; /* 1 = use mind-meld content encoder */
} wubu_model_dock_t;

/* Initialize the model dock.
 * Pre-allocates RCU slots for all 10 models. */
void wubu_model_dock_init(wubu_model_dock_t *dock);

/* Add a model to the dock (moves to front, evicts oldest if full).
 * If the model is already in the dock, just promote it.
 * Starts background async loading if not already loaded. */
int wubu_model_dock_add(wubu_model_dock_t *dock,
                        const char *pth_path,
                        const char *index_path,
                        const char *name,
                        int version);

/* Switch to a model by name (instant RCU swap).
 * Returns 0 if model is loaded and swapped, -1 if not ready. */
int wubu_model_dock_switch(wubu_model_dock_t *dock, const char *name);

/* Check if async loads have completed and swap in ready models.
 * Call from GUI timer (non-realtime thread). */
void wubu_model_dock_poll(wubu_model_dock_t *dock);

/* Get current active model (lock-free read from audio thread). */
void *wubu_model_dock_current(wubu_model_dock_t *dock);

/* Get model at index (for GUI display). */
const wubu_dock_entry_t *wubu_model_dock_entry(const wubu_model_dock_t *dock,
                                                int index);

/* Toggle mind-meld content encoder */
void wubu_model_dock_set_mind_meld(wubu_model_dock_t *dock, int enabled);

/* Enable/disable pre-warming all models in VRAM */
void wubu_model_dock_set_prewarm(wubu_model_dock_t *dock, int enabled);

/* Clean up all resources. */
void wubu_model_dock_destroy(wubu_model_dock_t *dock);

/* Save/load recent models list to config file. */
int wubu_model_dock_save(const wubu_model_dock_t *dock, const char *path);
int wubu_model_dock_load(wubu_model_dock_t *dock, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_MODEL_DOCK_H */

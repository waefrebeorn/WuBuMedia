#ifndef WUBU_RCU_H
#define WUBU_RCU_H

/* wubu_rcu.h — Read-Copy-Update for lock-free model hot-swapping.
 *
 * RCU pattern for real-time audio: the audio thread (reader) loads
 * the current model pointer atomically (0-cost, no locks), while
 * a background thread (writer) loads the next model and atomically
 * swaps it in. Old model is freed after the audio thread acknowledges.
 *
 * This enables parallel model loading: start loading model B in
 * background while model A is still serving real-time audio.
 *
 * C11 stdatomic — no pthreads or kernel RCU needed.
 *
 * License: WaefreBeorn-UMV3
 */

#include <stdatomic.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/* ── Model slot: atomic pointer to current model ── */
typedef struct {
    /* Atomic pointer — readers load this atomically (wait-free).
     * Writers swap with atomic_store + grace period. */
    atomic_uintptr_t current;     /* active model (read by audio thread) */
    atomic_uintptr_t pending;     /* loading model (written by bg thread) */

    /* Writer state — protected by writer_lock */
    void *old_model;              /* retired model awaiting grace period */
    atomic_int writer_active;     /* 1 = a write in progress */

    /* Grace period tracking */
    atomic_int reader_epoch;      /* even=reading A, odd=reading B */
    int pending_epoch;
} wubu_rcu_slot;

/* ── Background loader thread context ── */
typedef struct {
    char model_path[512];
    char index_path[512];
    char voice_name[128];
    int  version;
    void *loaded_model;          /* result: loaded model */
    int  error;                  /* 1 = error during load */
} wubu_load_task_t;

/* Initialize RCU slot */
void wubu_rcu_init(wubu_rcu_slot *slot, void *initial_model);

/* Reader-side critical section — call from audio thread.
 * Returns current model pointer (wait-free, 0 locks). */
#define wubu_rcu_read_lock(slot) \
    atomic_fetch_add(&(slot)->reader_epoch, 1)

#define wubu_rcu_read_unlock(slot) \
    atomic_fetch_add(&(slot)->reader_epoch, 1)

/* Get current model — safe inside read_lock/unlock */
static inline void *wubu_rcu_dereference(wubu_rcu_slot *slot) {
    return (void *)atomic_load_explicit(
        &(slot)->current, memory_order_acquire);
}

/* Writer: start async model load in background thread.
 * Returns 0 if load started, -1 if already in progress.
 * The actual model appears at `slot->current` after completion
 * and grace period. Non-blocking from audio thread perspective. */
int wubu_rcu_async_load(wubu_rcu_slot *slot, const char *pth_path,
                         const char *index_path, const char *voice_name,
                         int version);

/* Writer: synchronously swap in a pre-loaded model.
 * Frees the old model after grace period. */
void wubu_rcu_assign(wubu_rcu_slot *slot, void *new_model,
                     void (*model_free_fn)(void *));

/* Writer: wait for grace period (all readers done).
 * Called after assign to ensure old model is safe to free. */
void wubu_rcu_synchronize(wubu_rcu_slot *slot);

/* Check if async load completed and swap in if ready.
 * Must be called from non-realtime thread (e.g. GUI timer).
 * Returns 1 if swapped, 0 if still loading, -1 if error. */
int wubu_rcu_check_load(wubu_rcu_slot *slot, void (*model_free_fn)(void *));

/* Clean up RCU slot */
void wubu_rcu_destroy(wubu_rcu_slot *slot,
                      void (*model_free_fn)(void *));

/* ── Parallel batch loader ──
 * Loads N models in parallel threads, then swaps each into its slot. */
typedef struct {
    wubu_rcu_slot *slots;
    int n_models;
} wubu_rcu_set_t;

void wubu_rcu_set_init(wubu_rcu_set_t *set, int n_models, void **initial_models);
void wubu_rcu_set_load_all(wubu_rcu_set_t *set, const char **paths,
                           const char **index_paths, const char **names,
                           int *versions);
void wubu_rcu_set_destroy(wubu_rcu_set_t *set, void (*model_free_fn)(void *));

#endif /* WUBU_RCU_H */

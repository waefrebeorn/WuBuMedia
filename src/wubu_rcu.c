/* wubu_rcu.c — RCU implementation for lock-free model hot-swapping.
 *
 * Uses C11 stdatomic for wait-free reads (audio thread) and
 * background thread loading for writes (GUI thread).
 *
 * License: WaefreBeorn-UMV3
 */

#include "wubu_rcu.h"
#include "wubu_rvc_parity.h"  /* for wubu_rvc_load_model */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define THREAD_TYPE HANDLE
#define THREAD_RET void
#define THREAD_NULL NULL
static DWORD WINAPI loader_thread(LPVOID param) {
    wubu_load_task_t *task = (wubu_load_task_t *)param;
    task->loaded_model = wubu_rvc_load_model(task->model_path);
    task->error = (task->loaded_model == NULL) ? 1 : 0;
    return 0;
}
#define THREAD_CREATE(out, fn, arg) \
    (*(out) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(fn), (arg), 0, NULL))
#define THREAD_CLOSE(handle) CloseHandle((handle))
#else
#include <pthread.h>
#define THREAD_TYPE pthread_t
#define THREAD_RET void *
#define THREAD_NULL NULL
static void *loader_thread(void *param) {
    wubu_load_task_t *task = (wubu_load_task_t *)param;
    task->loaded_model = wubu_rvc_load_model(task->model_path);
    task->error = (task->loaded_model == NULL) ? 1 : 0;
    return NULL;
}
#define THREAD_CREATE(out, fn, arg) \
    pthread_create((out), NULL, (fn), (arg))
#define THREAD_CLOSE(handle) (void)(handle)
#endif

/* ── Single model slot ── */

void wubu_rcu_init(wubu_rcu_slot *slot, void *initial_model) {
    atomic_init(&slot->current, (uintptr_t)initial_model);
    atomic_init(&slot->pending, (uintptr_t)NULL);
    atomic_init(&slot->reader_epoch, 0);
    atomic_init(&slot->writer_active, 0);
    slot->old_model = NULL;
    slot->pending_epoch = 0;
}

int wubu_rcu_async_load(wubu_rcu_slot *slot, const char *pth_path,
                         const char *index_path, const char *voice_name,
                         int version) {
    /* Reject if already loading */
    int expected = 0;
    if (!atomic_compare_exchange_strong(&slot->writer_active, &expected, 1))
        return -1;

    wubu_load_task_t *task = (wubu_load_task_t *)calloc(1, sizeof(wubu_load_task_t));
    if (!task) {
        atomic_store(&slot->writer_active, 0);
        return -1;
    }
    strncpy(task->model_path, pth_path, sizeof(task->model_path) - 1);
    if (index_path) strncpy(task->index_path, index_path, sizeof(task->index_path) - 1);
    if (voice_name) strncpy(task->voice_name, voice_name, sizeof(task->voice_name) - 1);
    task->version = version;
    task->loaded_model = NULL;
    task->error = 0;

    /* Store pending task pointer */
    atomic_store(&slot->pending, (uintptr_t)task);

    /* Spawn background thread */
    THREAD_TYPE tid;
    if (THREAD_CREATE(&tid, loader_thread, task) == THREAD_NULL) {
        free(task);
        atomic_store(&slot->pending, 0);
        atomic_store(&slot->writer_active, 0);
        return -1;
    }

    /* Detach — result picked up by wubu_rcu_check_load() */
    THREAD_CLOSE(tid);
    return 0;
}

/* Check if async load completed and swap in if ready.
 * Must be called from non-realtime thread (e.g. GUI timer). */
int wubu_rcu_check_load(wubu_rcu_slot *slot, void (*model_free_fn)(void *)) {
    uintptr_t pending_ptr = atomic_load(&slot->pending);
    if (!pending_ptr) return 0;

    wubu_load_task_t *task = (wubu_load_task_t *)pending_ptr;

    /* task->loaded_model == NULL means either still loading or errored.
     * task->error == 1 means load completed with error. */
    if (task->error != 0 && task->loaded_model == NULL) {
        /* Load failed — clear pending */
        atomic_store(&slot->pending, 0);
        atomic_store(&slot->writer_active, 0);
        free(task);
        return -1;
    }

    if (task->loaded_model == NULL)
        return 0;  /* still loading */

    /* Load complete — swap atomically */
    void *old = (void *)atomic_exchange(&slot->current,
                                         (uintptr_t)task->loaded_model);
    atomic_store(&slot->pending, 0);
    atomic_store(&slot->writer_active, 0);
    slot->old_model = old;
    free(task);

    if (old && model_free_fn)
        model_free_fn(old);

    return 1;
}

void wubu_rcu_assign(wubu_rcu_slot *slot, void *new_model,
                     void (*model_free_fn)(void *)) {
    void *old = (void *)atomic_exchange(&slot->current, (uintptr_t)new_model);
    if (old && model_free_fn)
        model_free_fn(old);
}

void wubu_rcu_synchronize(wubu_rcu_slot *slot) {
    /* Grace period: ensure all readers have exited.
     * In RCU, readers are either inside or outside their critical section.
     * We advance the epoch twice to ensure any reader that started
     * before our write has completed. */
    atomic_thread_fence(memory_order_seq_cst);
    int epoch = atomic_load(&slot->reader_epoch);
    /* Wait for epoch to advance (all readers exit).
     * In single-threaded context, epoch already advanced. */
    int spins = 0;
    while (atomic_load(&slot->reader_epoch) == epoch && spins < 1000) {
#ifdef _WIN32
        Sleep(0);
#else
        struct timespec ts = {0, 100000};  /* 0.1ms */
        nanosleep(&ts, NULL);
#endif
        spins++;
    }
    /* Force one more epoch advance to be safe */
    atomic_fetch_add(&slot->reader_epoch, 1);
}

void wubu_rcu_destroy(wubu_rcu_slot *slot,
                      void (*model_free_fn)(void *)) {
    void *model = (void *)atomic_load(&slot->current);
    if (model && model_free_fn)
        model_free_fn(model);
    if (slot->old_model && model_free_fn)
        model_free_fn(slot->old_model);

    /* Free pending task if any */
    uintptr_t pending = atomic_load(&slot->pending);
    if (pending) {
        wubu_load_task_t *task = (wubu_load_task_t *)pending;
        if (task->loaded_model && model_free_fn)
            model_free_fn(task->loaded_model);
        free(task);
        atomic_store(&slot->pending, 0);
    }
}

/* ── Parallel batch loader ── */

void wubu_rcu_set_init(wubu_rcu_set_t *set, int n_models, void **initial_models) {
    set->n_models = n_models;
    set->slots = (wubu_rcu_slot *)calloc(n_models, sizeof(wubu_rcu_slot));
    for (int i = 0; i < n_models; i++)
        wubu_rcu_init(&set->slots[i], initial_models ? initial_models[i] : NULL);
}

void wubu_rcu_set_load_all(wubu_rcu_set_t *set, const char **paths,
                           const char **index_paths, const char **names,
                           int *versions) {
    /* Launch all loads in parallel */
    for (int i = 0; i < set->n_models; i++) {
        wubu_rcu_async_load(&set->slots[i], paths[i],
                            index_paths ? index_paths[i] : NULL,
                            names ? names[i] : NULL,
                            versions ? versions[i] : 2);
    }
    /* Give threads time to run, then check */
#ifdef _WIN32
    Sleep(200);
#else
    struct timespec ts = {0, 200000000};  /* 200ms */
    nanosleep(&ts, NULL);
#endif
    for (int i = 0; i < set->n_models; i++) {
        wubu_rcu_check_load(&set->slots[i], NULL);
    }
}

void wubu_rcu_set_destroy(wubu_rcu_set_t *set,
                          void (*model_free_fn)(void *)) {
    for (int i = 0; i < set->n_models; i++)
        wubu_rcu_destroy(&set->slots[i], model_free_fn);
    free(set->slots);
}

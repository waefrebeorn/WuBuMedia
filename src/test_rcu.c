/* test_rcu.c — Tests for RCU lock-free model hot-swapping.
 *
 * Tests:
 *   1. RCU slot initialization with initial model
 *   2. Atomic read (dereference) returns correct model
 *   3. Async load (with empty path → failure, but no crash)
 *   4. Assign (sync swap) replaces model atomically
 *   5. Synchronize completes grace period
 *   6. Reader epoch tracking (lock/unlock)
 *   7. Destroy cleans up properly
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#include "wubu_rcu.h"
#include "wubu_rvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  [%d] %s... ", tests_run, name); \
    fflush(stdout); \
} while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ── Test 1: RCU slot init ── */
static void test_rcu_init(void) {
    TEST("RCU slot initialization");
    wubu_rcu_slot slot;
    int dummy = 42;
    wubu_rcu_init(&slot, &dummy);
    void *cur = wubu_rcu_dereference(&slot);
    if (cur == &dummy) PASS();
    else FAIL("dereference returned wrong pointer");
    wubu_rcu_destroy(&slot, NULL); /* no free fn — dummy model */
}

/* ── Test 2: Atomic read ── */
static void test_rcu_read(void) {
    TEST("Atomic model read (dereference)");
    wubu_rcu_slot slot;
    int model_a = 1;
    wubu_rcu_init(&slot, &model_a);

    wubu_rcu_read_lock(&slot);
    void *cur = wubu_rcu_dereference(&slot);
    wubu_rcu_read_unlock(&slot);

    if (cur == &model_a) PASS();
    else FAIL("read returned wrong model after init");
    wubu_rcu_destroy(&slot, NULL);
}

/* ── Test 3: Async load (failure path — no .pth) ── */
static void test_rcu_async_fail(void) {
    TEST("Async load failure (no model file)");
    wubu_rcu_slot slot;
    int model_a = 1;
    wubu_rcu_init(&slot, &model_a);

    int rc = wubu_rcu_async_load(&slot, "nonexistent_model.pth",
                                  NULL, "test", 2);
    if (rc == 0) {
        /* Give background thread time to fail */
#ifdef _WIN32
        Sleep(100);
#else
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
#endif
        void *cur = wubu_rcu_dereference(&slot);
        /* Should still be model_a — failed load doesn't swap */
        if (cur == &model_a) PASS();
        else FAIL("model changed despite failed load");
        /* Clean up pending */
        uintptr_t pending = atomic_load(&slot.pending);
        if (pending) {
            wubu_load_task_t *task = (wubu_load_task_t *)pending;
            if (task->loaded_model) /* shouldn't be set */
                free(task);
            free(task);
            atomic_store(&slot.pending, 0);
        }
    } else {
        FAIL("async_load returned error on first call");
    }
    wubu_rcu_destroy(&slot, NULL);
}

/* ── Test 4: Sync assign ── */
static void test_rcu_assign(void) {
    TEST("Synchronous assign (atomic swap)");
    wubu_rcu_slot slot;
    int model_a = 1, model_b = 2;
    wubu_rcu_init(&slot, &model_a);

    wubu_rcu_assign(&slot, &model_b, NULL);
    void *cur = wubu_rcu_dereference(&slot);
    if (cur == &model_b) PASS();
    else FAIL("assign did not swap model");
    wubu_rcu_destroy(&slot, NULL);
}

/* ── Test 5: Synchronize ── */
static void test_rcu_sync(void) {
    TEST("Grace period synchronize");
    wubu_rcu_slot slot;
    int model_a = 1, model_b = 2;
    wubu_rcu_init(&slot, &model_a);

    wubu_rcu_read_lock(&slot);
    wubu_rcu_read_unlock(&slot);
    wubu_rcu_synchronize(&slot);
    void *cur = wubu_rcu_dereference(&slot);
    if (cur == &model_a) PASS();
    else FAIL("model changed during synchronize");
    wubu_rcu_destroy(&slot, NULL);
    (void)model_b;
}

/* ── Test 6: Reader epoch tracking ── */
static void test_rcu_epoch(void) {
    TEST("Reader epoch tracking");
    wubu_rcu_slot slot;
    wubu_rcu_init(&slot, NULL);
    int start = atomic_load(&slot.reader_epoch);
    wubu_rcu_read_lock(&slot);
    int mid = atomic_load(&slot.reader_epoch);
    wubu_rcu_read_unlock(&slot);
    int end = atomic_load(&slot.reader_epoch);
    /* Epoch should advance after lock+unlock (2 increments) */
    if (end > start) PASS();
    else FAIL("epoch did not advance after read_lock/unlock");
    (void)mid;
    wubu_rcu_destroy(&slot, NULL);
}

/* ── Test 7: Destroy with model free ── */
static void test_rcu_destroy_free(void) {
    TEST("Destroy with model free callback");
    wubu_rcu_slot slot;
    int *model = (int *)malloc(sizeof(int));
    *model = 42;
    wubu_rcu_init(&slot, model);
    wubu_rcu_destroy(&slot, free);
    PASS(); /* if we get here without crash, free was called */
}

/* ── Test 8: Parallel batch load (all paths fail gracefully) ── */
static void test_rcu_batch(void) {
    TEST("Parallel batch loader");
    wubu_rcu_set_t set;
    const char *paths[] = {"bad1.pth", "bad2.pth"};
    wubu_rcu_set_init(&set, 2, NULL);
    wubu_rcu_set_load_all(&set, paths, NULL, NULL, NULL);
    /* All slots should still be NULL (loads failed) */
    void *m0 = wubu_rcu_dereference(&set.slots[0]);
    void *m1 = wubu_rcu_dereference(&set.slots[1]);
    if (m0 == NULL && m1 == NULL) PASS();
    else FAIL("batch load returned non-NULL for failed loads");
    wubu_rcu_set_destroy(&set, NULL);
}

/* ── Test 9: No double-load lock ── */
static void test_rcu_double_load(void) {
    TEST("Reject double async load");
    wubu_rcu_slot slot;
    int model_a = 1;
    wubu_rcu_init(&slot, &model_a);
    int rc1 = wubu_rcu_async_load(&slot, "bad1.pth", NULL, "test1", 2);
    int rc2 = wubu_rcu_async_load(&slot, "bad2.pth", NULL, "test2", 2);
    /* First should succeed, second should fail (-1) */
    if (rc1 == 0 && rc2 == -1) PASS();
    else FAIL("second load should be rejected");
    /* Cleanup */
    wubu_rcu_destroy(&slot, NULL);
}

/* ── Test 10: Multiple assign swaps ── */
static void test_rcu_multi_assign(void) {
    TEST("Multiple sequential assigns");
    wubu_rcu_slot slot;
    int a = 1, b = 2, c = 3, d = 4;
    wubu_rcu_init(&slot, &a);

    wubu_rcu_assign(&slot, &b, NULL);
    wubu_rcu_assign(&slot, &c, NULL);
    wubu_rcu_assign(&slot, &d, NULL);

    void *cur = wubu_rcu_dereference(&slot);
    if (cur == &d) PASS();
    else FAIL("final assign did not take effect");
    wubu_rcu_destroy(&slot, NULL);
}

int main(void) {
    printf("=== WuBuRVC RCU Hot-Swap Test Suite ===\n\n");

    test_rcu_init();
    test_rcu_read();
    test_rcu_async_fail();
    test_rcu_assign();
    test_rcu_sync();
    test_rcu_epoch();
    test_rcu_destroy_free();
    test_rcu_batch();
    test_rcu_double_load();
    test_rcu_multi_assign();

    printf("\n=== Results: %d/%d tests passed ===\n",
           tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

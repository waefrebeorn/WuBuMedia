/* test_cohost.c — Test harness for wubu_cohost (C11).
 * SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -Wall -Wextra -std=c11 test_cohost.c wubu_cohost.c wubu_wiki.c wubu_emotion.c wubu_rlm.c wubu_recs.c -lsqlite3 -lm -o test_cohost.exe -g
 */
#include "wubu_cohost.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static const char *current_test = "";

#define TEST(name)  do { current_test = name; tests_run++; printf("[RUN] %s\n", name); } while(0)
#define PASS()      do { tests_passed++; printf("[PASS] %s\n", current_test); } while(0)
#define FAIL(fmt, ...) do { printf("[FAIL] %s — " fmt "\n", current_test, ##__VA_ARGS__); } while(0)

int main(void) {
    printf("=== wubu_cohost C11 Test Suite ===\n\n");

    char dbpath[] = "/tmp/test_cohost.db";
    remove(dbpath);
    remove("/tmp/wubu_recs.db");
    remove("/tmp/wubu_rlm.db");

    /* ---- Test 1: Lifecycle ---- */
    TEST("lifecycle create/destroy");
    Cohost *c = wubu_cohost_create(dbpath, "WuBu");
    if (c) PASS(); else { FAIL("create failed"); return 1; }

    /* ---- Test 2: Persona ---- */
    TEST("persona name + prompt");
    const char *name = wubu_cohost_name(c);
    const char *prompt = wubu_cohost_prompt(c);
    if (name && strcmp(name, "WuBu") == 0 && prompt && strlen(prompt) > 10)
        PASS();
    else FAIL("name=%s prompt_len=%zu", name, prompt ? strlen(prompt) : 0);

    /* ---- Test 3: Emotion update ---- */
    TEST("emotion update");
    wubu_cohost_update_emotion(c, "I'm so excited about this AGI build!");
    CohostStats stats;
    wubu_cohost_stats(c, &stats);
    if (stats.energy >= 0.0 && stats.energy <= 1.0 &&
        stats.mood >= 0.0 && stats.mood <= 1.0)
        PASS();
    else FAIL("energy=%.2f mood=%.2f", stats.energy, stats.mood);

    /* ---- Test 4: Memory (remember) ---- */
    TEST("remember message");
    int mem_ok = wubu_cohost_remember(c, "user", "Hello WuBu, I love building AGI on Windows!");
    if (mem_ok) PASS(); else FAIL("remember failed");

    /* ---- Test 5: Recall ---- */
    TEST("recall history");
    /* Add enough exchanges to trigger summarization (threshold=400 tokens ~ 1600 chars) */
    for (int i = 0; i < 50; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Message %d about AGI development on Windows with CUDA and cohost streaming persona.", i);
        wubu_cohost_remember(c, "user", msg);
    }
    char *recalled = wubu_cohost_recall(c, "AGI");
    if (recalled && strlen(recalled) > 0) {
        PASS();
        free(recalled);
    } else {
        if (recalled) free(recalled);
        FAIL("no recall results");
    }

    /* ---- Test 6: Store fact ---- */
    TEST("store fact");
    int fact_ok = wubu_cohost_store_fact(c, "user.os_preference", "Windows", 0.9);
    if (fact_ok) PASS(); else FAIL("store_fact failed");

    /* ---- Test 7: Lookup fact ---- */
    TEST("lookup fact");
    wubu_cohost_store_fact(c, "app.name", "WuBuMedia", 1.0);
    WikiFact wf;
    int lookup_ok = wubu_cohost_lookup(c, "app.name", &wf);
    if (lookup_ok && wf.value && strcmp(wf.value, "WuBuMedia") == 0) {
        PASS();
        wubu_wiki_free_fact(&wf);
    } else {
        if (lookup_ok) wubu_wiki_free_fact(&wf);
        FAIL("lookup: ok=%d", lookup_ok);
    }

    /* ---- Test 8: Register + recommend content ---- */
    TEST("content recommend");
    Recs *recs = wubu_cohost_recs(c);
    if (recs) {
        wubu_recs_register_content(recs, "rec-001", "WuBuOS Kernel Design",
            "The WuBuOS kernel has 8 C11 modules: task, sched, mem, vmm, pmm, rtm, syscall, idt",
            "agi,os,kernel", 1200.0);
        wubu_recs_register_content(recs, "rec-002", "CUDA GQA Implementation",
            "Grouped-query attention with SSD-paged MoE in CUDA", "cuda,agi,moe", 900.0);

        wubu_recs_log_event(recs, "test_user", "rec-001", RECS_VIEW, 0, 0);
        wubu_recs_log_event(recs, "test_user", "rec-001", RECS_PLAY, 600, 1200);
        wubu_recs_log_event(recs, "test_user", "rec-001", RECS_LIKE, 0, 0);
        wubu_recs_log_event(recs, "test_user", "rec-002", RECS_VIEW, 0, 0);
        wubu_recs_log_event(recs, "test_user", "rec-002", RECS_PLAY, 800, 900);
    }

    RecsCandidate recs_out[10];
    size_t n_recs = wubu_cohost_recommend(c, "test_user", recs_out, 10);
    if (n_recs > 0) PASS(); else FAIL("no recommendations");
    wubu_recs_free_candidates(recs_out, n_recs);

    /* ---- Test 9: Stats aggregation ---- */
    TEST("cohort stats");
    CohostStats full_stats;
    wubu_cohost_stats(c, &full_stats);
    if (full_stats.rlm_stats.facts >= 1 &&
        full_stats.recs_stats.total_videos >= 2)
        PASS();
    else FAIL("rlm_facts=%zu recs_videos=%zu",
              full_stats.rlm_stats.facts, full_stats.recs_stats.total_videos);

    /* ---- Test 10: Stress — many exchanges ---- */
    TEST("stress: 30 exchanges");
    int ok = 1;
    for (int i = 0; i < 30; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Stress test message %d about cohost development.", i);
        if (!wubu_cohost_remember(c, "user", msg)) ok = 0;
    }
    if (ok) PASS(); else FAIL("stress test failed");

    wubu_cohost_destroy(c);
    remove(dbpath);
    remove("/tmp/wubu_recs.db");
    remove("/tmp/wubu_rlm.db");

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

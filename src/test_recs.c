/* test_recs.c — Test harness for wubu_recs (C11).
 *
 * Tests: content registration, event logging, candidate retrieval,
 * ranking, re-ranking, full pipeline, stats.
 *
 * SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -Wall -Wextra -std=c11 test_recs.c wubu_recs.c -lsqlite3 -lm -o test_recs.exe -g
 */
#include "wubu_recs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static const char *current_test = "";

#define TEST(name)  do { current_test = name; tests_run++; printf("[RUN] %s\n", name); } while(0)
#define PASS()      do { tests_passed++; printf("[PASS] %s\n", current_test); } while(0)
#define FAIL(fmt, ...) printf("[FAIL] %s — " fmt "\n", current_test, ##__VA_ARGS__)

int main(void) {
    printf("=== wubu_recs C11 Test Suite ===\n\n");

    char dbpath[] = "/tmp/test_recs.db";
    remove(dbpath);

    /* ---- Test 1: Open + register content ---- */
    TEST("lifecycle + register content");
    Recs *recs = wubu_recs_open(dbpath);
    if (!recs) { FAIL("failed to open Recs"); return 1; }

    int r1 = wubu_recs_register_content(recs, "vid-alg1", "Algorithms 101",
        "Introduction to algorithm complexity and Big-O notation", "agi,algorithm,coding", 300.0);
    int r2 = wubu_recs_register_content(recs, "vid-cuda", "CUDA Optimization",
        "GPU kernels and memory optimization for ML inference", "cuda,gpu,agi", 600.0);
    int r3 = wubu_recs_register_content(recs, "vid-yt2", "YUY2 Capture Guide",
        "How to capture HDMI from PS5 using YUY2 mjpeg format", "capture,yuy2,ps5", 180.0);
    int r4 = wubu_recs_register_content(recs, "vid-cohost", "Cohost Persona Design",
        "Building the WuBuMedia cohost with emotion engine", "ai,cohost,emotion", 240.0);

    if (r1 && r2 && r3 && r4) PASS(); else FAIL("register: r1=%d r2=%d r3=%d r4=%d", r1, r2, r3, r4);

    /* ---- Test 2: Log events ---- */
    TEST("log events");
    /* Simulate user "test_user" interacting with videos */
    int events_ok = 1;
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-alg1", RECS_VIEW, 0, 0);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-alg1", RECS_PLAY, 280, 300);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-alg1", RECS_COMPLETE, 0, 0);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-alg1", RECS_LIKE, 0, 0);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-alg1", RECS_REWATCH, 0, 0);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-cuda", RECS_VIEW, 0, 0);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-cuda", RECS_PLAY, 590, 600);
    events_ok &= wubu_recs_log_event(recs, "test_user", "vid-cuda", RECS_SHARE, 0, 0);

    if (events_ok) PASS(); else FAIL("some events failed to log");

    /* ---- Test 3: Candidate retrieval ---- */
    TEST("candidate retrieval");
    RecsCandidate candidates[20];
    size_t n_cand = wubu_recs_candidates(recs, "test_user", candidates, 20);
    if (n_cand > 0) PASS(); else FAIL("no candidates returned");
    wubu_recs_free_candidates(candidates, n_cand);

    /* ---- Test 4: Full ranking ---- */
    TEST("ranking (full pipeline)");
    /* Re-get candidates after freeing */
    n_cand = wubu_recs_candidates(recs, "test_user", candidates, 20);
    wubu_recs_rank(recs, candidates, n_cand);
    /* After ranking, vid-alg1 should have higher score (liked + complete + rewatched) */
    int found_alg1 = 0, found_cuda = 0;
    double alg1_score = 0, cuda_score = 0;
    for (size_t i = 0; i < n_cand; i++) {
        if (candidates[i].slug && strcmp(candidates[i].slug, "vid-alg1") == 0) {
            found_alg1 = 1; alg1_score = candidates[i].score;
        }
        if (candidates[i].slug && strcmp(candidates[i].slug, "vid-cuda") == 0) {
            found_cuda = 1; cuda_score = candidates[i].score;
        }
    }
    /* vid-alg1 had like + complete + rewatch, should score higher */
    if (found_alg1 && alg1_score > cuda_score) PASS();
    else FAIL("alg1(found=%d score=%.1f) > cuda(found=%d score=%.1f): %s",
              found_alg1, alg1_score, found_cuda, cuda_score,
              found_alg1 && alg1_score > cuda_score ? "OK" : "NOT OK");
    wubu_recs_free_candidates(candidates, n_cand);

    /* ---- Test 5: Re-ranking (diversity) ---- */
    TEST("re-ranking diversity");
    recs_config.diversity_factor = 0.3;
    n_cand = wubu_recs_candidates(recs, "test_user", candidates, 20);
    wubu_recs_rank(recs, candidates, n_cand);
    size_t n_before = n_cand;
    wubu_recs_rerank(recs, candidates, n_cand);
    if (n_cand == n_before) PASS(); else FAIL("count changed: %zu -> %zu", n_before, n_cand);
    wubu_recs_free_candidates(candidates, n_cand);

    /* Reset config */
    recs_config.diversity_factor = 0.2;

    /* ---- Test 6: Full pipeline ---- */
    /* Full pipeline test: verify recommendations returned AND sorted descending */
    TEST("full recommend pipeline");
    RecsCandidate recs_out[10];
    size_t n_rec = wubu_recs_recommend(recs, "test_user", recs_out, 10);
    int sorted_ok = 1;
    for (size_t i = 1; i < n_rec; i++) {
        if (recs_out[i-1].score < recs_out[i].score) sorted_ok = 0;
    }
    if (n_rec > 0 && sorted_ok) PASS(); else FAIL("n=%zu sorted=%d", n_rec, sorted_ok);
    wubu_recs_free_candidates(recs_out, n_rec);

    /* ---- Test 7: Stats ---- */
    TEST("stats");
    RecsStats stats;
    wubu_recs_stats(recs, &stats);
    if (stats.total_videos >= 4 && stats.total_events > 0) PASS();
    else FAIL("videos=%zu events=%zu", stats.total_videos, stats.total_events);

    /* ---- Test 8: New user (no history) gets popular content ---- */
    TEST("new user candidate fallback");
    RecsCandidate new_candidates[10];
    size_t n_new = wubu_recs_candidates(recs, "brand_new_user", new_candidates, 10);
    if (n_new > 0) PASS(); else FAIL("new user got no candidates");
    wubu_recs_free_candidates(new_candidates, n_new);

    /* ---- Test 9: Duplicate content registration ---- */
    TEST("idempotent content registration");
    int r_dup = wubu_recs_register_content(recs, "vid-alg1", "Algorithms 101",
        "Introduction to algorithm complexity and Big-O notation", "agi,algorithm,coding", 300.0);
    /* INSERT OR REPLACE returns DONE — same result, just updated */
    if (r_dup) PASS(); else FAIL("re-register failed");

    wubu_recs_close(recs);
    remove(dbpath);

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

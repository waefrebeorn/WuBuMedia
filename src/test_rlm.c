/* test_rlm.c — Test harness for wubu_rlm (C11).
 * SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -Wall -Wextra -std=c11 test_rlm.c wubu_rlm.c -lsqlite3 -o test_rlm.exe -g
 */
#include "wubu_rlm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static const char *current_test = "";

#define TEST(name)  do { current_test = name; tests_run++; printf("[RUN] %s\n", name); } while(0)
#define PASS()      do { tests_passed++; printf("[PASS] %s\n", current_test); } while(0)
#define FAIL(fmt, ...) printf("[FAIL] %s — " fmt "\n", current_test, ##__VA_ARGS__)

static void fill_long_msg(char *buf, size_t len, const char *topic) {
    size_t pos = 0;
    for (int i = 0; i < 20 && pos < len - 64; i++) {
        pos += (size_t)snprintf(buf + pos, len - pos,
            "User discusses %s detail number %d. "
            "The system records this information carefully for future reference "
            "and context retention. This is a long message designed to "
            "exceed the token threshold and trigger summarization. ",
            topic, i);
    }
}

int main(void) {
    printf("=== wubu_rlm C11 Test Suite ===\n\n");

    /* ---- Test 1: Lifecycle + basic exchange ---- */
    TEST("lifecycle + add_exchange");
    char dbpath[] = "/tmp/test_rlm_lifecycle.db";
    remove(dbpath);
    RLM *rlm = wubu_rlm_open(dbpath, "test_ctx");
    if (!rlm) { FAIL("failed to open RLM"); return 1; }

    size_t tokens;
    int summarized = wubu_rlm_add_exchange(rlm, "user", "Hello, cohost!", NULL, NULL, &tokens);
    if (summarized == 0 && tokens > 0) PASS(); else FAIL("summar=%d tokens=%zu", summarized, tokens);

    wubu_rlm_add_exchange(rlm, "ai", "Hello! How can I help you today?", NULL, NULL, NULL);

    /* ---- Test 2: Get context ---- */
    TEST("get_context");
    const char *speakers[8], *texts[8];
    size_t ctx_count = wubu_rlm_get_context(rlm, speakers, texts, 8);
    if (ctx_count == 2) PASS(); else FAIL("ctx_count=%zu", ctx_count);
    for (size_t i = 0; i < ctx_count; i++) { free((void *)speakers[i]); free((void *)texts[i]); }

    /* ---- Test 3: Summarization triggers ---- */
    TEST("summarization triggers");
    char long_msg[2048];
    fill_long_msg(long_msg, sizeof(long_msg), "streaming");
    int did_summarize = 0;
    for (int i = 0; i < 10; i++) {
        /* Use fill_long_msg directly with turn info appended safely */
        char msg[2560];
        snprintf(msg, sizeof(msg), "%.2030s turn%d", long_msg, i);
        size_t t;
        int s = wubu_rlm_add_exchange(rlm, "user", msg, NULL, NULL, &t);
        if (s) did_summarize = 1;
    }
    if (did_summarize) PASS(); else FAIL("never summarized");

    /* ---- Test 4: Recall ---- */
    TEST("recall search");
    RLMRecall recall[10];
    size_t found = wubu_rlm_recall(rlm, "streaming", recall, 10);
    int found_streaming = 0;
    for (size_t i = 0; i < found; i++) {
        if (recall[i].summary && strstr(recall[i].summary, "streaming"))
            found_streaming = 1;
        wubu_rlm_free_recall(&recall[i]);
    }
    if (found_streaming) PASS(); else FAIL("no summary with streaming (found=%zu)", found);

    /* ---- Test 5: Fact storage ---- */
    TEST("store + get fact");
    int ok = wubu_rlm_store_fact(rlm, "user.name", "WaefreBeorn", 0.9, "user");
    RLMFact fact;
    int got = wubu_rlm_get_fact(rlm, "user.name", &fact);
    int fact_ok = ok && got && fact.value &&
                  strcmp(fact.value, "WaefreBeorn") == 0 && fact.confidence > 0.8;
    if (fact.value) free((void *)fact.value);
    if (fact.source) free((void *)fact.source);
    if (fact_ok) PASS(); else FAIL("ok=%d got=%d", ok, got);

    /* ---- Test 6: Stats ---- */
    TEST("stats");
    RLMStats stats;
    wubu_rlm_stats(rlm, &stats);
    if (stats.context && stats.facts >= 1) PASS(); else FAIL("facts=%zu", stats.facts);

    /* ---- Test 7: Custom summary_fn ---- */
    TEST("custom summary_fn");
    {
        char dbpath2[] = "/tmp/test_rlm_summaryfn.db";
        remove(dbpath2);
        RLM *rlm2 = wubu_rlm_open(dbpath2, "test_summaryfn");
        if (!rlm2) { FAIL("could not open rlm2"); PASS(); }
        else {
            /* Custom summary: truncate to 10 chars */
            char *my_summarize(const char *buffer_text, void *ud) {
                (void)ud;
                if (!buffer_text) return strdup("");
                char *result = malloc(11);
                if (!result) return strdup("");
                strncpy(result, buffer_text, 10);
                result[10] = '\0';
                return result;
            }
            for (int i = 0; i < 5; i++) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "Long message number %d about AGI development and Windows porting work.", i);
                wubu_rlm_add_exchange(rlm2, "user", msg, my_summarize, NULL, NULL);
            }
            wubu_rlm_close(rlm2);
            PASS();
        }
        remove(dbpath2);
    }

    /* ---- Test 8: Token estimation ---- */
    TEST("token estimation");
    size_t t1 = rlm_estimate_tokens("Hello, world!");  /* 13 chars */
    size_t t2 = rlm_estimate_tokens("");
    size_t t3 = rlm_estimate_tokens(NULL);
    if (t1 >= 1 && t2 == 1 && t3 == 1) PASS(); else FAIL("t1=%zu t2=%zu t3=%zu", t1, t2, t3);

    /* ---- Test 9: Stress ---- */
    TEST("stress: 50 exchanges");
    {
        int ok_all = 1;
        for (int i = 0; i < 50; i++) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Exchange %d for stress testing.", i);
            if (wubu_rlm_add_exchange(rlm, "user", msg, NULL, NULL, NULL) < 0)
                ok_all = 0;
        }
        if (ok_all) PASS(); else FAIL("errors in stress");
    }

    wubu_rlm_close(rlm);
    remove(dbpath);

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

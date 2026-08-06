/* test_agent.c — Test harness for wubu_agent (C11).
 * SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -Wall -Wextra -std=c11 test_agent.c wubu_agent.c wubu_cohost.c wubu_wiki.c wubu_emotion.c wubu_rlm.c wubu_recs.c -lsqlite3 -lm -o test_agent.exe -g
 */
#include "wubu_agent.h"
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
    printf("=== wubu_agent C11 Test Suite ===\n\n");

    remove("/tmp/test_agent_cohost.db");
    remove("/tmp/wubu_recs.db");
    remove("/tmp/wubu_rlm.db");

    /* ---- Test 1: Agent lifecycle ---- */
    TEST("agent create/destroy");
    Cohost *c = wubu_cohost_create("/tmp/test_agent_cohost.db", "TestAgent");
    if (!c) { FAIL("cohost create failed"); return 1; }
    WubuAgent *a = wubu_agent_create(NULL, c);
    if (a) PASS(); else { FAIL("agent create failed"); wubu_cohost_destroy(c); return 1; }

    /* ---- Test 2: Build context ---- */
    TEST("build context");
    char *ctx = wubu_agent_build_context(a, "How do I build C11 agents?");
    if (ctx && strlen(ctx) > 0) {
        PASS();
        free(ctx);
    } else {
        if (ctx) free(ctx);
        FAIL("context too short");
    }

    /* ---- Test 3: Chat ---- */
    TEST("chat with agent");
    char *resp = wubu_agent_chat(a, "I'm excited about this AGI build on Windows!");
    if (resp && strlen(resp) > 0) {
        PASS();
        free(resp);
    } else {
        if (resp) free(resp);
        FAIL("empty response");
    }

    /* ---- Test 4: Store + recall facts ---- */
    TEST("store + lookup fact");
    wubu_cohost_store_fact(c, "platform", "Windows", 0.95);
    WikiFact wf;
    if (wubu_cohost_lookup(c, "platform", &wf) && wf.value &&
        strcmp(wf.value, "Windows") == 0) {
        PASS();
        wubu_wiki_free_fact(&wf);
    } else {
        if (wf.value) wubu_wiki_free_fact(&wf);
        FAIL("fact lookup failed");
    }

    /* ---- Test 5: Stats ---- */
    TEST("agent stats");
    wubu_cohost_remember(c, "user", "Another message about AGI development on RTX 2080");
    wubu_cohost_remember(c, "user", "And another about CUDA kernels and memory management");
    wubu_cohost_remember(c, "user", "Plus one about MSYS2 toolchain integration");
    wubu_cohost_remember(c, "user", "And streaming setup with OBS WebSocket");
    wubu_cohost_remember(c, "user", "With USB 3.0 HDMI capture device");
    resp = wubu_agent_chat(a, "Tell me about our AGI cohost architecture.");
    WubuAgentStats st;
    wubu_agent_stats(a, &st);
    if (st.total_exchanges >= 2 && st.total_tokens > 0) {
        PASS();
    } else {
        FAIL("exchanges=%zu tokens=%zu", st.total_exchanges, st.total_tokens);
    }
    free(resp);

    /* ---- Test 6: Recall after conversation ---- */
    TEST("recall after conversation");
    /* Add enough context to trigger summarization */
    for (int i = 0; i < 30; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Exchange %d discussing Windows AGI cohost porting, CUDA kernels, RTX 2080, MSYS2 toolchain, and streaming infrastructure.", i);
        wubu_cohost_remember(c, "user", msg);
    }
    char *recalled = wubu_cohost_recall(c, "AGI");
    if (recalled && strlen(recalled) > 0) {
        PASS();
        free(recalled);
    } else {
        if (recalled) free(recalled);
        FAIL("no recall");
    }

    /* ---- Test 7: Multiple chats ---- */
    TEST("multiple chats");
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Chat message %d about Windows AGI porting.", i);
        resp = wubu_agent_chat(a, msg);
        if (!resp || strlen(resp) == 0) ok = 0;
        free(resp);
    }
    if (ok) PASS(); else FAIL("multi-chat failed");

    /* ---- Test 8: Stats accuracy ---- */
    TEST("stats accuracy");
    wubu_agent_stats(a, &st);
    if (st.total_exchanges >= 7) {
        PASS();
    } else {
        FAIL("only %zu exchanges", st.total_exchanges);
    }

    wubu_agent_destroy(a);
    wubu_cohost_destroy(c);
    remove("/tmp/test_agent_cohost.db");
    remove("/tmp/wubu_recs.db");
    remove("/tmp/wubu_rlm.db");

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

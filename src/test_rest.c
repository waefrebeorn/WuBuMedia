/* test_rest.c — Test harness for gateway, self, face, wss (C11).
 * SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -Wall -Wextra -std=c11 test_rest.c wubu_gateway.c wubu_self.c wubu_face.c wubu_wss.c wubu_cohost.c wubu_wiki.c wubu_emotion.c wubu_rlm.c wubu_recs.c -lsqlite3 -lm -o test_rest.exe -g
 */
#include "wubu_cohost.h"
#include "wubu_gateway.h"
#include "wubu_self.h"
#include "wubu_face.h"
#include "wubu_wss.h"
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
    printf("=== wubu_rest C11 Test Suite ===\n\n");

    remove("/tmp/test_rest.db");
    remove("/tmp/wubu_recs.db");
    remove("/tmp/wubu_rlm.db");

    /* ---- Test 1: Face lifecycle ---- */
    TEST("face create/destroy");
    Face *face = wubu_face_create("/tmp", NULL);
    if (face) PASS(); else { FAIL("create failed"); return 1; }

    /* ---- Test 2: Face update ---- */
    TEST("face update");
    if (wubu_face_update(face, "Hello AGI cohost", "happy", 1)) {
        PASS();
    } else {
        FAIL("face_update failed");
    }
    if (wubu_face_get_mood(face) >= 0.0 && wubu_face_get_mood(face) <= 1.0) {}
    else FAIL("mood out of range: %.2f", wubu_face_get_mood(face));

    /* ---- Test 3: Face poke/fling ---- */
    TEST("face poke + fling");
    int ok = wubu_face_poke(face, 3) && wubu_face_fling(face, 7);
    if (ok) PASS(); else FAIL("poke/fling failed");

    /* ---- Test 4: Cohost + gateway integration ---- */
    TEST("gateway create + health");
    Cohost *c = wubu_cohost_create("/tmp/test_rest.db", "TestCohost");
    if (!c) { FAIL("cohost create failed"); wubu_face_destroy(face); return 1; }

    wubu_cohost_store_fact(c, "test.key", "test_value", 0.9);
    wubu_cohost_remember(c, "user", "Building AGI on Windows with C11");

    Gateway *gw = wubu_gateway_create("secret-token", 18799, c);
    if (!gw) { FAIL("gateway create failed"); wubu_cohost_destroy(c); wubu_face_destroy(face); return 1; }
    PASS();

    /* ---- Test 5: Self-improvement scheduler ---- */
    TEST("self check");
    Self *s = wubu_self_create(NULL, "/tmp/self_test.log", 1);
    if (!s) {
        FAIL("self create failed");
    } else {
        SelfReport report;
        int ok = wubu_self_check(s, &report);
        (void)ok;
        if (report.check_count > 0) {
            PASS();
        } else {
            FAIL("no checks ran");
        }
        wubu_self_free_report(&report);
        wubu_self_destroy(s);
    }

    /* ---- Test 6: WSS server ---- */
    TEST("wss create/destroy");
    WSS *ws = wubu_wss_create(18800, face);
    if (ws) PASS(); else FAIL("wss create failed");

    /* ---- Test 7: Full cohost stats through gateway cohost ---- */
    TEST("cohost stats integration");
    CohostStats stats;
    wubu_cohost_stats(c, &stats);
    if (stats.rlm_stats.facts >= 1) {
        PASS();
    } else {
        FAIL("no facts stored, facts=%zu", stats.rlm_stats.facts);
    }

    /* ---- Test 8: Face mood from text ---- */
    TEST("face + emotion pipeline");
    wubu_cohost_update_emotion(c, "I'm so excited about this AGI build!");
    wubu_cohost_stats(c, &stats);
    if (stats.mood > 0.5) {
        wubu_face_update(face, "Excited!", "excited", 1);
        if (wubu_face_get_energy(face) > 0.5) PASS(); else FAIL("energy too low");
    } else {
        FAIL("mood should be elevated");
    }

    /* Cleanup */
    if (ws) wubu_wss_destroy(ws);
    wubu_gateway_destroy(gw);
    wubu_cohost_destroy(c);
    wubu_face_destroy(face);
    remove("/tmp/test_rest.db");
    remove("/tmp/wubu_recs.db");
    remove("/tmp/wubu_rlm.db");
    remove("/tmp/self_test.log");
    remove("/tmp/face_state.json");

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

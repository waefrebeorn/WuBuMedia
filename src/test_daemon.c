/* test_daemon.c — Test harness for wubu_daemon (C11).
 *
 * Tests the JSON parser, command dispatch, and daemon lifecycle
 * without needing the actual named pipe server running.
 *
 * Build: cc -Wall -Wextra -std=c11 test_daemon.c wubu_daemon.c wubu_wiki.c -lsqlite3 -I . -o test_daemon
 */
#include "wubu_daemon.h"
#include "wubu_wiki.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_pass = 0;

#define RUN_TEST(name, func) do { \
    tests_run++; \
    printf("[RUN] %s\n", name); \
    fflush(stdout); \
    if (func()) { tests_pass++; printf("[PASS] %s\n", name); } \
    else { printf("[FAIL] %s\n", name); } \
    fflush(stdout); \
} while(0)

static int test_json_parse(void) {
    Daemon *d = wubu_daemon_start(NULL, "/tmp/test_wiki_daemon.db");
    if (!d) return 0;

    char resp[8192];
    int rc = wubu_daemon_handle_command(d, "{\"cmd\":\"ping\"}", resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "\"ok\":true")) { wubu_daemon_free(d); return 0; }

    rc = wubu_daemon_handle_command(d, "{\"cmd\":\"stats\"}", resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "articles")) { wubu_daemon_free(d); return 0; }

    wubu_wiki_put_fact(wubu_daemon_wiki(d), "test.key", "test-value", 0.9, "test");
    rc = wubu_daemon_handle_command(d, "{\"cmd\":\"wiki_fact\",\"key\":\"test.key\"}", resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "test-value")) { wubu_daemon_free(d); return 0; }

    rc = wubu_daemon_handle_command(d, "{\"cmd\":\"emotion_get\"}", resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "\"mood\":\"neutral\"")) { wubu_daemon_free(d); return 0; }

    rc = wubu_daemon_handle_command(d, "{\"cmd\":\"emotion_set\",\"mood\":\"happy\"}", resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "\"new\":\"happy\"")) { wubu_daemon_free(d); return 0; }

    rc = wubu_daemon_handle_command(d, "{\"cmd\":\"invalid_cmd\"}", resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "unknown command")) { wubu_daemon_free(d); return 0; }

    rc = wubu_daemon_handle_command(d, "not json at all", resp, sizeof(resp));
    if (rc != -1 || !strstr(resp, "invalid JSON")) { wubu_daemon_free(d); return 0; }

    rc = wubu_daemon_handle_command(d, "{\"foo\":\"bar\"}", resp, sizeof(resp));
    if (rc != -1 || !strstr(resp, "missing")) { wubu_daemon_free(d); return 0; }

    wubu_daemon_stop(d);
    wubu_daemon_free(d);
    return 1;
}

static int test_wiki_integration(void) {
    remove("/tmp/test_wiki_daemon2.db");
    Daemon *d = wubu_daemon_start(NULL, "/tmp/test_wiki_daemon2.db");
    if (!d) return 0;

    const char *fk[] = {"capture.format"};
    const char *fv[] = {"YUY2"};
    int changed = wubu_wiki_upsert(wubu_daemon_wiki(d), "test-article", "Test Article",
        "YUY2 capture format for USB HDMI devices", "capture",
        "repo", NULL, fk, fv, 1);
    if (changed != 1) { wubu_daemon_free(d); return 0; }

    char resp[8192];
    int rc = wubu_daemon_handle_command(d, "{\"cmd\":\"wiki_search\",\"query\":\"YUY2\"}",
        resp, sizeof(resp));
    if (rc != 0 || !strstr(resp, "test-article")) { wubu_daemon_free(d); return 0; }

    changed = wubu_wiki_upsert(wubu_daemon_wiki(d), "test-article", "Test Article",
        "YUY2 capture format for USB HDMI devices", "capture",
        "repo", NULL, fk, fv, 1);
    if (changed != 0) { wubu_daemon_free(d); return 0; }

    wubu_daemon_stop(d);
    wubu_daemon_free(d);
    return 1;
}

int main(void) {
    printf("=== wubu_daemon C11 Test Suite ===\n\n");
    fflush(stdout);

    RUN_TEST("json_parse + command dispatch", test_json_parse);
    RUN_TEST("wiki upsert + search", test_wiki_integration);

    printf("\n=== Results: %d/%d tests passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}

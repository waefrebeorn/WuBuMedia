/* test_wiki.c — Test harness for wubu_wiki (C11).
 *
 * Builds: cc -Wall -Wextra -std=c11 test_wiki.c wubu_wiki.c -lsqlite3 -o test_wiki
 * Run:   ./test_wiki
 *
 * License: SPDX-License-Identifier: WaefreBeorn-UMV3
 */
#include "wubu_wiki.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static void test_open_close(void) {
    Wiki *w = wubu_wiki_open("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki.db");
    assert(w != NULL);
    printf("[PASS] wubu_wiki_open\n");

    WikiStats stats;
    wubu_wiki_stats(w, &stats);
    assert(stats.articles == 0);
    assert(stats.facts == 0);
    printf("[PASS] stats (articles=0, facts=0)\n");

    wubu_wiki_close(w);
    printf("[PASS] wubu_wiki_close\n");
}

static void test_upsert_search(void) {
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki2.db");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki2.db-wal");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki2.db-shm");
    Wiki *w = wubu_wiki_open("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki2.db");
    assert(w != NULL);

    /* Upsert a test article with YUY2 content */
    const char *fk[] = {"test.capture_format", "test.device"};
    const char *fv[] = {"YUY2", "Logitech C920"};
    int changed = wubu_wiki_upsert(w, "capture-yuy2", "YUY2 Capture Format",
        "YUY2 is a packed pixel format used by USB capture devices like the "
        "Logitech C920. The Monster HDMI capture from Walmart uses YUY2 "
        "for low-latency streaming.", "capture,uvc",
        "repo", "https://github.com/waefrebeorn/WuBuMedia",
        fk, fv, 2);
    assert(changed == 1);
    printf("[PASS] wubu_wiki_upsert (changed=%d)\n", changed);

    /* Idempotency: same content should return 0 */
    changed = wubu_wiki_upsert(w, "capture-yuy2", "YUY2 Capture Format",
        "YUY2 is a packed pixel format used by USB capture devices like the "
        "Logitech C920. The Monster HDMI capture from Walmart uses YUY2 "
        "for low-latency streaming.", "capture,uvc",
        "repo", "https://github.com/waefrebeorn/WuBuMedia",
        fk, fv, 2);
    assert(changed == 0);
    printf("[PASS] idempotency (changed=%d)\n", changed);

    /* Search for YUY2 */
    WikiResult results[10];
    size_t n = wubu_wiki_search(w, "YUY2", results, 10, NULL);
    assert(n > 0);
    printf("[PASS] search 'YUY2' -> %zu results (score=%.4f)\n", n, results[0].score);
    assert(results[0].slug != NULL);
    assert(strstr(results[0].slug, "capture") != NULL);
    printf("[PASS] result slug: %s\n", results[0].slug);

    /* Free search results */
    for (size_t i = 0; i < n; i++) wubu_wiki_free_result(&results[i]);

    /* Search with tag filter */
    n = wubu_wiki_search(w, "Logitech", results, 10, "capture");
    assert(n > 0);
    printf("[PASS] filtered search 'Logitech' tag=capture -> %zu results\n", n);
    for (size_t i = 0; i < n; i++) wubu_wiki_free_result(&results[i]);

    /* Get by slug */
    WikiResult r;
    int found = wubu_wiki_get(w, "capture-yuy2", &r);
    assert(found == 1);
    printf("[PASS] get by slug: title='%s' tags='%s'\n", r.title, r.tags);
    wubu_wiki_free_result(&r);

    /* List articles */
    WikiResult list_buf[10];
    size_t ln = wubu_wiki_list(w, list_buf, 10, NULL, NULL);
    assert(ln > 0);
    printf("[PASS] list -> %zu articles\n", ln);
    for (size_t i = 0; i < ln; i++) wubu_wiki_free_result(&list_buf[i]);

    wubu_wiki_close(w);
}

static void test_facts(void) {
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki3.db");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki3.db-wal");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki3.db-shm");
    Wiki *w = wubu_wiki_open("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki3.db");
    assert(w != NULL);

    /* Put a fact */
    int ok = wubu_wiki_put_fact(w, "test.capture.format", "YUY2", 0.9, "tool");
    assert(ok == 1);
    printf("[PASS] wubu_wiki_put_fact\n");

    /* Get the fact */
    WikiFact f;
    ok = wubu_wiki_get_fact(w, "test.capture.format", &f);
    assert(ok == 1);
    printf("[PASS] wubu_wiki_get_fact: key='%s' value='%s' conf=%.2f source='%s'\n",
           f.key, f.value, f.confidence, f.source);
    assert(strcmp(f.value, "YUY2") == 0);
    wubu_wiki_free_fact(&f);

    /* List facts */
    WikiFact flist[10];
    size_t fn = wubu_wiki_list_facts(w, flist, 10);
    assert(fn > 0);
    printf("[PASS] list_facts -> %zu facts\n", fn);
    for (size_t i = 0; i < fn; i++) wubu_wiki_free_fact(&flist[i]);

    /* Missing fact */
    ok = wubu_wiki_get_fact(w, "nonexistent.key", &f);
    assert(ok == 0);
    printf("[PASS] missing fact returns 0\n");

    wubu_wiki_close(w);
}

static void test_auto_tags(void) {
    char *tags = wubu_wiki_auto_tags(
        "This article discusses YUY2 capture format and browser cookies. "
        "Twitch IRC integration is also covered.");
    printf("[INFO] auto_tags: %s\n", tags ? tags : "(null)");
    assert(tags != NULL);
    assert(strstr(tags, "capture") != NULL);
    assert(strstr(tags, "browser") != NULL);
    assert(strstr(tags, "twitch") != NULL);
    printf("[PASS] auto_tags extracted capture, browser, twitch\n");
    wubu_wiki_free_string(tags);
}

static void test_links(void) {
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki_links.db");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki_links.db-wal");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki_links.db-shm");
    Wiki *w = wubu_wiki_open("C:/Users/eman5/WuBuMedia/tmp_msvc/test_wiki_links.db");
    assert(w != NULL);

    /* Upsert two articles that cross-reference each other */
    const char *c1 = "wubu_persona manages the cohost avatar and wubu_ears.";
    const char *c2 = "wubu_ears handles audio input and references wubu_persona.";
    assert(wubu_wiki_upsert(w, "src-wubu_persona", "Persona",
        c1, "persona", "repo", NULL, NULL, NULL, 0) == 1);
    assert(wubu_wiki_upsert(w, "src-wubu_ears", "Ears",
        c2, "audio", "repo", NULL, NULL, NULL, 0) == 1);
    printf("[PASS] upsert two cross-referencing articles\n");

    /* Manual link */
    assert(wubu_wiki_link(w, "src-wubu_persona", "src-wubu_ears", "related") == 1);
    assert(wubu_wiki_link(w, "src-wubu_ears", "src-wubu_persona", "related") == 1);
    printf("[PASS] wubu_wiki_link (manual)\n");

    /* Idempotent: re-link should still return 1 (INSERT OR REPLACE) */
    assert(wubu_wiki_link(w, "src-wubu_persona", "src-wubu_ears", "related") == 1);
    printf("[PASS] wubu_wiki_link idempotent\n");

    /* Stats should show 2 links */
    WikiStats stats;
    wubu_wiki_stats(w, &stats);
    assert(stats.links == 2);
    printf("[PASS] stats.links == %zu (expected 2)\n", stats.links);

    /* Auto-links: add a third article whose content references both */
    const char *c3 = "wubu_persona and wubu_ears are used together in cohost.";
    assert(wubu_wiki_upsert(w, "src-wubu_cohost", "Cohost",
        c3, "agi", "repo", NULL, NULL, NULL, 0) == 1);

    /* Remove the manual links first to test auto_links cleanly */
    wubu_wiki_clear_links(w);
    /* stats.links should be 0 now */
    wubu_wiki_stats(w, &stats);
    assert(stats.links == 0);
    printf("[PASS] links cleared for auto_links test\n");

    /* Run auto_links */
    size_t auto_count = wubu_wiki_auto_links(w);
    printf("[PASS] wubu_wiki_auto_links created %zu links\n", auto_count);
    assert(auto_count > 0);

    /* Verify bidirectional links exist */
    wubu_wiki_stats(w, &stats);
    assert(stats.links >= 2);
    printf("[PASS] stats.links == %zu after auto_links\n", stats.links);

    wubu_wiki_close(w);
}

int main(void) {
    printf("=== wubu_wiki C11 Test Suite ===\n\n");
    test_open_close();
    test_upsert_search();
    test_facts();
    test_auto_tags();
    test_links();
    printf("\n=== All tests passed ===\n");
    return 0;
}

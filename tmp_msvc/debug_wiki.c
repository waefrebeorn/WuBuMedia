/* debug_wiki.c — Debug the C wiki search and link functionality */
#include "wubu_wiki.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* Declared in wubu_wiki.c; header may be auto-reverted, so declare here */
struct sqlite3;
extern struct sqlite3 *wubu_wiki_db(Wiki *w);

/* Raw SQL query to verify data in the database */
static void raw_check(Wiki *w) {
    sqlite3 *db = wubu_wiki_db(w);
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT slug, title, content, tags, source, source_url FROM articles";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    printf("  raw SQL prepare: %d\n", rc);
    int row = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        row++;
        printf("  RAW row %d: slug='%s' title='%s' content='%.40s' tags='%s'\n",
            row,
            sqlite3_column_text(stmt, 0) ? (const char*)sqlite3_column_text(stmt, 0) : "(null)",
            sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "(null)",
            sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "(null)",
            sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "(null)");
    }
    printf("  raw rows: %d\n", row);
    sqlite3_finalize(stmt);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Clean slate */
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/debug_wiki.db");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/debug_wiki.db-wal");
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/debug_wiki.db-shm");

    const char *content = "YUY2 is a packed pixel format. It is similar to YUY2.";
    const char *fact_keys[] = {"format", "vendor"};
    const char *fact_values[] = {"YUY2", "Unknown"};

    Wiki *w = wubu_wiki_open("C:/Users/eman5/WuBuMedia/tmp_msvc/debug_wiki.db");
    assert(w != NULL);
    printf("OK: wiki opened\n");

    /* Get raw sqlite handle for diagnostic query */
    sqlite3 *db = wubu_wiki_db(w);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE name='articles'", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("Schema: %s\n", sqlite3_column_text(stmt, 0) ? (const char*)sqlite3_column_text(stmt, 0) : "(null)");
    }
    sqlite3_finalize(stmt);

    /* Check journal mode */
    sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("Journal mode: %s\n", sqlite3_column_text(stmt, 0) ? (const char*)sqlite3_column_text(stmt, 0) : "(null)");
    }
    sqlite3_finalize(stmt);

    printf("\n--- Upsert ---\n");
    int changed = wubu_wiki_upsert(w, "capture-yuy2", "YUY2 Capture Format",
        content, "capture,uvc", "repo",
        "https://github.com/waefrebeorn/WuBuMedia",
        fact_keys, fact_values, 2);
    printf("upsert changed=%d (expected 1)\n", changed);
    assert(changed == 1);

    /* Raw check: see what's actually in the DB */
    printf("\n--- Raw DB check ---\n");
    raw_check(w);

    printf("\n--- Search ---\n");
    WikiResult results[10];
    size_t n = wubu_wiki_search(w, "YUY2", results, 10, NULL);
    printf("search 'YUY2' returned %zu results\n", n);
    for (size_t i = 0; i < n; i++) {
        printf("  [%zu] slug='%s' title='%s' tags='%s' score=%.4f\n",
            i, results[i].slug ? results[i].slug : "(null)",
            results[i].title ? results[i].title : "(null)",
            results[i].tags ? results[i].tags : "(null)",
            results[i].score);
        wubu_wiki_free_result(&results[i]);
    }

    printf("\n--- Get ---\n");
    WikiResult r;
    memset(&r, 0, sizeof(r));
    int found = wubu_wiki_get(w, "capture-yuy2", &r);
    printf("get by slug: found=%d\n", found);
    if (found) {
        printf("  slug='%s' title='%s' tags='%s'\n",
            r.slug ? r.slug : "(null)",
            r.title ? r.title : "(null)",
            r.tags ? r.tags : "(null)");
        wubu_wiki_free_result(&r);
    }

    printf("\n--- Get fact ---\n");
    WikiFact f;
    memset(&f, 0, sizeof(f));
    int ok = wubu_wiki_get_fact(w, "format", &f);
    printf("get_fact: ok=%d value='%s' conf=%.2f\n",
        ok, f.value ? f.value : "(null)", f.confidence);
    wubu_wiki_free_fact(&f);

    wubu_wiki_close(w);
    printf("\n=== Done ===\n");
    return 0;
}

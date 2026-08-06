/* minimal_fts5_test.c — Isolate FTS5 column_text issue */
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    sqlite3 *db;
    char *err = NULL;
    int rc;

    rc = sqlite3_open_v2("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_test.db", &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        printf("Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    printf("DB opened OK\n");

    /* Check FTS5 availability */
    rc = sqlite3_exec(db, "CREATE VIRTUAL TABLE IF NOT EXISTS test_articles USING fts5(title, slug, content, tags);", NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        printf("FTS5 CREATE failed: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(db);
        return 1;
    }
    printf("FTS5 table created OK\n");

    /* Insert a row */
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO test_articles (title, slug, content, tags) VALUES (?, ?, ?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("Prepare INSERT failed: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, "Test Title", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, "test-slug", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, "This is test content with YUY2 data", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, "tag1,tag2", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    printf("INSERT step: %d (=%s)\n", rc, rc == SQLITE_DONE ? "DONE" : "OTHER");
    sqlite3_finalize(stmt);

    /* Query back with SELECT */
    rc = sqlite3_prepare_v2(db, "SELECT slug, title, content, tags FROM test_articles", -1, &stmt, NULL);
    printf("SELECT prepare: %d\n", rc);
    rc = sqlite3_step(stmt);
    printf("SELECT step: %d (=%s)\n", rc, rc == SQLITE_ROW ? "ROW" : "OTHER");
    if (rc == SQLITE_ROW) {
        const char *slug = (const char *)sqlite3_column_text(stmt, 0);
        const char *title = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *tags = (const char *)sqlite3_column_text(stmt, 3);
        printf("  RAW: slug='%s' title='%s' content='%s' tags='%s'\n",
               slug ? slug : "(null)",
               title ? title : "(null)",
               content ? content : "(null)",
               tags ? tags : "(null)");
        /* Now try strdup */
        char *slug_copy = strdup(slug ? slug : "");
        char *title_copy = strdup(title ? title : "");
        printf("  DUP: slug='%s' title='%s'\n", slug_copy, title_copy);
        free(slug_copy);
        free(title_copy);
    }
    sqlite3_finalize(stmt);

    /* Query with MATCH */
    rc = sqlite3_prepare_v2(db, "SELECT slug, title FROM test_articles WHERE test_articles MATCH ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, "YUY2", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    printf("\nMATCH step: %d (=%s)\n", rc, rc == SQLITE_ROW ? "ROW" : "OTHER");
    if (rc == SQLITE_ROW) {
        const char *slug = (const char *)sqlite3_column_text(stmt, 0);
        const char *title = (const char *)sqlite3_column_text(stmt, 1);
        printf("  MATCH: slug='%s' title='%s'\n",
               slug ? slug : "(null)",
               title ? title : "(null)");
    }
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    printf("\n=== Done ===\n");
    return 0;
}

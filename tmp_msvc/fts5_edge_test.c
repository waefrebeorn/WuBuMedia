/* fts5_edge_test.c — Test UNINDEXED columns and 'content' column name in FTS5 */
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    sqlite3 *db;
    char *err = NULL;
    int rc;

    /* Test A: schema matching wubu_wiki (with 'content' column + UNINDEXED) */
    rc = sqlite3_open_v2("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_edge.db", &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_edge.db");
    sqlite3_close(db);
    rc = sqlite3_open_v2("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_edge.db", &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        printf("Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    printf("=== Test A: 'content' column + UNINDEXED ===\n");
    rc = sqlite3_exec(db,
        "CREATE VIRTUAL TABLE articles USING fts5("
        "title UNINDEXED, slug UNINDEXED, content, "
        "tags UNINDEXED, source UNINDEXED, "
        "created UNINDEXED, updated UNINDEXED, source_url);",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        printf("CREATE failed: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(db);
        return 1;
    }
    printf("Table created OK\n");

    /* Insert */
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO articles (title, slug, content, tags, source, created, updated, source_url) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    printf("INSERT prepare: %d\n", rc);
    sqlite3_bind_text(stmt, 1, "YUY2 Capture Format", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, "capture-yuy2", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, "YUY2 is a packed pixel format", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, "capture,uvc", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, "repo", -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, 1000.0);
    sqlite3_bind_double(stmt, 7, 1000.0);
    sqlite3_bind_text(stmt, 8, "https://github.com/test", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    printf("INSERT step: %d (=%s)\n", rc, rc == SQLITE_DONE ? "DONE" : "OTHER");
    sqlite3_finalize(stmt);

    /* SELECT by slug */
    printf("\n--- SELECT by slug ---\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT slug, title, content, tags, source, source_url FROM articles WHERE slug=?",
        -1, &stmt, NULL);
    printf("SELECT prepare: %d\n", rc);
    sqlite3_bind_text(stmt, 1, "capture-yuy2", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    printf("SELECT step: %d (=%s)\n", rc, rc == SQLITE_ROW ? "ROW" : "OTHER");
    if (rc == SQLITE_ROW) {
        for (int i = 0; i < 6; i++) {
            const char *val = (const char *)sqlite3_column_text(stmt, i);
            printf("  col[%d]: '%s'\n", i, val ? val : "(null)");
        }
    }
    sqlite3_finalize(stmt);

    /* SELECT with MATCH */
    printf("\n--- SELECT with MATCH ---\n");
    rc = sqlite3_prepare_v2(db,
        "SELECT slug, title, content, tags, source, source_url, bm25(articles) as score "
        "FROM articles WHERE articles MATCH ?",
        -1, &stmt, NULL);
    printf("MATCH prepare: %d\n", rc);
    sqlite3_bind_text(stmt, 1, "YUY2", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    printf("MATCH step: %d (=%s)\n", rc, rc == SQLITE_ROW ? "ROW" : "OTHER");
    if (rc == SQLITE_ROW) {
        for (int i = 0; i < 7; i++) {
            const char *val = (const char *)sqlite3_column_text(stmt, i);
            if (i < 6)
                printf("  col[%d]: '%s'\n", i, val ? val : "(null)");
            else
                printf("  col[%d] (double): %.4f\n", i, sqlite3_column_double(stmt, i));
        }
    }
    sqlite3_finalize(stmt);

    sqlite3_close(db);

    /* Test B: schema without 'content' column name */
    printf("\n=== Test B: 'content_data' column (no 'content' keyword) ===\n");
    rc = sqlite3_open_v2("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_edge2.db", &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    remove("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_edge2.db");
    sqlite3_close(db);
    rc = sqlite3_open_v2("C:/Users/eman5/WuBuMedia/tmp_msvc/fts5_edge2.db", &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    rc = sqlite3_exec(db,
        "CREATE VIRTUAL TABLE articles USING fts5("
        "title UNINDEXED, slug UNINDEXED, content_data, "
        "tags UNINDEXED, source UNINDEXED, "
        "created UNINDEXED, updated UNINDEXED, source_url);",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        printf("CREATE B failed: %s\n", err ? err : "unknown");
        sqlite3_free(err);
    } else {
        printf("Table B created OK\n");
        rc = sqlite3_prepare_v2(db,
            "INSERT INTO articles (title, slug, content_data, tags, source, created, updated, source_url) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 2, "capture-yuy2", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, "YUY2 is a packed pixel format", -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        rc = sqlite3_prepare_v2(db,
            "SELECT slug, title, content_data, tags FROM articles WHERE slug=?",
            -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, "capture-yuy2", -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        printf("SELECT step: %d\n", rc);
        if (rc == SQLITE_ROW) {
            for (int i = 0; i < 4; i++) {
                const char *val = (const char *)sqlite3_column_text(stmt, i);
                printf("  col[%d]: '%s'\n", i, val ? val : "(null)");
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);

    printf("\n=== Done ===\n");
    return 0;
}

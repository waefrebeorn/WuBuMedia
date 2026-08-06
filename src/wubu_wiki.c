/* wubu_wiki.c — Knowledge base implementation (C11, SQLite FTS5).
 *
 * Full-text searchable knowledge base for the cohost AGI.
 * Uses SQLite's FTS5 virtual table with BM25 ranking.
 *
 * The struct WikiImpl is opaque — only functions in this file
 * touch its internals.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_wiki.c -o wubu_wiki.o -lsqlite3 -Wall -Wextra
 */
#include "wubu_wiki.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

/* ---------- Opaque struct (hidden from consumers) ---------- */
struct WikiImpl {
    sqlite3 *db;
    char    *db_path;
};

/* ---------- Schema string (FTS5 + regular tables) ---------- */
static const char *SCHEMA =
    "CREATE VIRTUAL TABLE IF NOT EXISTS articles USING fts5("
    "    title UNINDEXED,"
    "    slug UNINDEXED,"
    "    content,"
    "    tags UNINDEXED,"
    "    source UNINDEXED,"
    "    created UNINDEXED,"
    "    updated UNINDEXED,"
    "    source_url UNINDEXED"
    ");"
    "CREATE TABLE IF NOT EXISTS facts ("
    "    key TEXT PRIMARY KEY,"
    "    value TEXT,"
    "    confidence REAL DEFAULT 1.0,"
    "    source TEXT,"
    "    created REAL,"
    "    updated REAL"
    ");"
    "CREATE TABLE IF NOT EXISTS links ("
    "    from_slug TEXT,"
    "    to_slug TEXT,"
    "    kind TEXT DEFAULT 'related',"
    "    PRIMARY KEY (from_slug, to_slug, kind)"
    ");"
    "CREATE TABLE IF NOT EXISTS checkpoints ("
    "    slug TEXT PRIMARY KEY,"
    "    hash TEXT UNIQUE,"
    "    updated REAL"
    ");";

/* ---------- Minimal djb2 hash for change detection ---------- */
static void wubu_hash(const char *s, char out[17]) {
    unsigned long h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = "0123456789abcdef"[(h >> (i * 8 + 4)) & 0xF];
        out[i * 2 + 1] = "0123456789abcdef"[(h >> (i * 8)) & 0xF];
    }
    out[16] = '\0';
}

/* ---------- Auto-tag patterns ---------- */
struct tag_rule { const char *pattern; const char *tag; };
static const struct tag_rule TAG_RULES[] = {
    { "yuy2|mjpeg|capture|uvc|usb",      "capture"  },
    { "hotkey|registerhotkey|win32",     "system"   },
    { "cookie|browser|chrome|dpapi",     "browser"  },
    { "twitch|irc|privmsg|usernotice",   "twitch"   },
    { "obs|websocket|scene|source",      "obs"      },
    { "voice|speak|viseme|ducking",      "voice"    },
    { "emotion|prosody|mood|persona",    "persona"  },
    { "spring|physics|avatar|fling",     "avatar"   },
    { "watchdog|restart|memory|crash",   "ops"      },
    { "agi|agent|control|master",        "agi"      },
    { NULL, NULL }
};

/* Check if any pipe-separated alternative in pattern appears in content
 * (case-insensitive). strstr() does literal matching, so we split on '|'
 * and test each alternative individually. */
static int _pattern_matches(const char *content, const char *pattern) {
    if (!content || !pattern) return 0;
    size_t clen = strlen(content);
    char *lower_content = malloc(clen + 1);
    if (!lower_content) return 0;
    for (size_t i = 0; i < clen; i++)
        lower_content[i] = (char)tolower((unsigned char)content[i]);
    lower_content[clen] = '\0';

    int found = 0;
    size_t plen = strlen(pattern);
    char *pat_copy = malloc(plen + 1);
    if (!pat_copy) { free(lower_content); return 0; }
    memcpy(pat_copy, pattern, plen + 1);

    char *start = pat_copy;
    char *p = pat_copy;
    while (1) {
        if (*p == '|' || *p == '\0') {
            int is_last = (*p == '\0');
            *p = '\0';
            for (char *c = start; *c; c++)
                *c = (char)tolower((unsigned char)*c);
            if (strstr(lower_content, start)) { found = 1; break; }
            if (is_last) break;
            start = p + 1;
        }
        p++;
    }

    free(pat_copy);
    free(lower_content);
    return found;
}

char *wubu_wiki_auto_tags(const char *content) {
    if (!content) return NULL;
    char buf[256] = "";
    size_t len = 0;
    for (size_t i = 0; TAG_RULES[i].pattern; i++) {
        if (!_pattern_matches(content, TAG_RULES[i].pattern)) continue;
        const char *tag = TAG_RULES[i].tag;
        size_t tlen = strlen(tag);
        char *check = strstr(buf, tag);
        int already = 0;
        if (check) {
            char before = check == buf ? ',' : *(check - 1);
            char after = check[tlen];
            if ((before == ',' || before == '\0') &&
                (after == ',' || after == '\0')) {
                already = 1;
            }
        }
        if (!already && len + tlen + 2 < sizeof(buf)) {
            if (len > 0) { buf[len++] = ','; }
            memcpy(buf + len, tag, tlen);
            len += tlen;
        }
    }
    buf[len] = '\0';
    return len > 0 ? strdup(buf) : NULL;
}

void wubu_wiki_free_string(char *s) {
    free(s);
}

/* ---------- Lifecycle ---------- */
Wiki *wubu_wiki_open(const char *db_path) {
    if (!db_path) return NULL;
    Wiki *w = (Wiki *)calloc(1, sizeof(Wiki));
    if (!w) return NULL;
    w->db_path = strdup(db_path);
    if (!w->db_path) { free(w); return NULL; }

    int rc = sqlite3_open_v2(db_path, &w->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_wiki: cannot open %s: %s\n",
                db_path, sqlite3_errmsg(w->db));
        sqlite3_close(w->db);
        free(w->db_path);
        free(w);
        return NULL;
    }

    /* Enable WAL mode for concurrent reads */
    char *err = NULL;
    rc = sqlite3_exec(w->db,
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA foreign_keys=ON;", NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_wiki: pragma error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
    }

    /* Create schema */
    rc = sqlite3_exec(w->db, SCHEMA, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_wiki: schema error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(w->db);
        free(w->db_path);
        free(w);
        return NULL;
    }
    return w;
}

void wubu_wiki_close(Wiki *w) {
    if (!w) return;
    if (w->db) sqlite3_close(w->db);
    free(w->db_path);
    free(w);
}

/* ---------- Article operations ---------- */
int wubu_wiki_upsert(Wiki *w, const char *slug, const char *title,
                     const char *content, const char *tags,
                     const char *source, const char *source_url,
                     const char *const *fact_keys,
                     const char *const *fact_values,
                     size_t fact_count) {
    if (!w || !slug || !content) return -1;

    char hash[17];
    wubu_hash(content, hash);
    double now = (double)time(NULL);

    sqlite3_stmt *stmt = NULL;
    int rc;

    /* Check if content is unchanged */
    rc = sqlite3_prepare_v2(w->db,
        "SELECT hash FROM checkpoints WHERE slug=? AND hash=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, slug, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return 0; /* Unchanged */
        }
        sqlite3_finalize(stmt);
    }

    /* Delete old article (FTS5 doesn't support UPDATE well) */
    rc = sqlite3_prepare_v2(w->db,
        "DELETE FROM articles WHERE slug=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, slug, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    /* Merge auto-tags with provided tags */
    char *auto_tags = wubu_wiki_auto_tags(content);
    char *all_tags = NULL;
    if (tags && auto_tags) {
        size_t tl = strlen(tags) + 1 + strlen(auto_tags) + 1;
        all_tags = (char *)malloc(tl);
        if (all_tags) {
            snprintf(all_tags, tl, "%s,%s", tags, auto_tags);
        }
    } else if (tags) {
        all_tags = strdup(tags);
    } else if (auto_tags) {
        all_tags = strdup(auto_tags);
    }
    if (auto_tags) free(auto_tags);

    /* Insert new article */
    const char *insert_sql =
        "INSERT INTO articles (title, slug, content, tags, source, "
        "created, updated, source_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    rc = sqlite3_prepare_v2(w->db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(all_tags);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, title ? title : slug, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, all_tags ? all_tags : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, source ? source : "learning", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, now);
    sqlite3_bind_double(stmt, 7, now);
    sqlite3_bind_text(stmt, 8, source_url, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(all_tags);

    if (rc != SQLITE_DONE) return -1;

    /* Update checkpoint */
    rc = sqlite3_prepare_v2(w->db,
        "INSERT OR REPLACE INTO checkpoints (slug, hash, updated) VALUES (?, ?, ?)",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, slug, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    /* Insert/update facts */
    for (size_t i = 0; i < fact_count; i++) {
        if (fact_keys[i] && fact_values[i]) {
            sqlite3_stmt *fstmt = NULL;
            rc = sqlite3_prepare_v2(w->db,
                "INSERT OR REPLACE INTO facts (key, value, confidence, source, created, updated) "
                "VALUES (?, ?, 1.0, ?, ?, ?)", -1, &fstmt, NULL);
            if (rc == SQLITE_OK) {
                sqlite3_bind_text(fstmt, 1, fact_keys[i], -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fstmt, 2, fact_values[i], -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(fstmt, 3, source ? source : "learning", -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(fstmt, 4, now);
                sqlite3_bind_double(fstmt, 5, now);
                sqlite3_step(fstmt);
                sqlite3_finalize(fstmt);
            }
        }
    }

    return 1;
}

/* ---------- Search ---------- */
size_t wubu_wiki_search(Wiki *w, const char *query,
                        WikiResult *results, size_t limit,
                        const char *tag_filter) {
    if (!w || !query || !results || limit == 0) return 0;

    sqlite3_stmt *stmt = NULL;
    const char *sql;
    int rc;

    if (tag_filter) {
        sql = "SELECT slug, title, content, tags, source, source_url, "
              "bm25(articles) as score "
              "FROM articles "
              "WHERE articles MATCH ? AND tags LIKE ? "
              "ORDER BY rank LIMIT ?";
        rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return 0;
        char fts_query[512];
        char tag_pattern[256];
        snprintf(fts_query, sizeof(fts_query), "%s", query);
        snprintf(tag_pattern, sizeof(tag_pattern), "%%%s%%", tag_filter);
        sqlite3_bind_text(stmt, 1, fts_query, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, tag_pattern, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, (int)limit);
    } else {
        sql = "SELECT slug, title, content, tags, source, source_url, "
              "bm25(articles) as score "
              "FROM articles "
              "WHERE articles MATCH ? "
              "ORDER BY rank LIMIT ?";
        rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) return 0;
        sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, (int)limit);
    }

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
        WikiResult *r = &results[count];
        const char *s0 = (const char *)sqlite3_column_text(stmt, 0);
        const char *s1 = (const char *)sqlite3_column_text(stmt, 1);
        const char *s2 = (const char *)sqlite3_column_text(stmt, 2);
        const char *s3 = (const char *)sqlite3_column_text(stmt, 3);
        const char *s4 = (const char *)sqlite3_column_text(stmt, 4);
        const char *s5 = (const char *)sqlite3_column_text(stmt, 5);
        /* Copy into malloc'd memory — SQLite buffers are invalidated on finalize */
        r->slug       = s0 ? strdup(s0) : NULL;
        r->title      = s1 ? strdup(s1) : NULL;
        r->content    = s2 ? strdup(s2) : NULL;
        r->tags       = s3 ? strdup(s3) : NULL;
        r->source     = s4 ? strdup(s4) : NULL;
        r->source_url = s5 ? strdup(s5) : NULL;
        r->score      = -sqlite3_column_double(stmt, 6); /* BM25 returns negative */
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---------- Get article ---------- */
int wubu_wiki_get(Wiki *w, const char *slug, WikiResult *out) {
    if (!w || !slug || !out) return 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT slug, title, content, tags, source, source_url "
                      "FROM articles WHERE slug=?";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, slug, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    const char *s0 = (const char *)sqlite3_column_text(stmt, 0);
    const char *s1 = (const char *)sqlite3_column_text(stmt, 1);
    const char *s2 = (const char *)sqlite3_column_text(stmt, 2);
    const char *s3 = (const char *)sqlite3_column_text(stmt, 3);
    const char *s4 = (const char *)sqlite3_column_text(stmt, 4);
    const char *s5 = (const char *)sqlite3_column_text(stmt, 5);
    /* Copy into malloc'd memory — SQLite buffers are invalidated on finalize */
    out->slug       = s0 ? strdup(s0) : NULL;
    out->title      = s1 ? strdup(s1) : NULL;
    out->content    = s2 ? strdup(s2) : NULL;
    out->tags       = s3 ? strdup(s3) : NULL;
    out->source     = s4 ? strdup(s4) : NULL;
    out->source_url = s5 ? strdup(s5) : NULL;
    out->score      = 0.0;
    sqlite3_finalize(stmt);
    return 1;
}

/* ---------- List articles ---------- */
size_t wubu_wiki_list(Wiki *w, WikiResult *out, size_t limit,
                      const char *tag_filter, const char *source_filter) {
    if (!w || !out) return 0;

    sqlite3_stmt *stmt = NULL;
    char sql[512];
    int offset = 0;
    int rc;

    offset += snprintf(sql + offset, sizeof(sql) - offset,
                       "SELECT slug, title, content, tags, source, source_url, 0.0 "
                       "FROM articles WHERE 1=1");
    int arg = 1;
    if (tag_filter) {
        offset += snprintf(sql + offset, sizeof(sql) - offset,
                           " AND tags LIKE ?");
    }
    if (source_filter) {
        offset += snprintf(sql + offset, sizeof(sql) - offset,
                           " AND source=?");
    }
    offset += snprintf(sql + offset, sizeof(sql) - offset,
                       " ORDER BY updated DESC LIMIT ?");

    rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;

    if (tag_filter) {
        char pat[256];
        snprintf(pat, sizeof(pat), "%%%s%%", tag_filter);
        sqlite3_bind_text(stmt, arg, pat, -1, SQLITE_TRANSIENT); arg++;
    }
    if (source_filter) {
        sqlite3_bind_text(stmt, arg, source_filter, -1, SQLITE_STATIC); arg++;
    }
    sqlite3_bind_int(stmt, arg, (int)limit);

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
        WikiResult *r = &out[count];
        const char *s0 = (const char *)sqlite3_column_text(stmt, 0);
        const char *s1 = (const char *)sqlite3_column_text(stmt, 1);
        const char *s2 = (const char *)sqlite3_column_text(stmt, 2);
        const char *s3 = (const char *)sqlite3_column_text(stmt, 3);
        const char *s4 = (const char *)sqlite3_column_text(stmt, 4);
        const char *s5 = (const char *)sqlite3_column_text(stmt, 5);
        /* Copy into malloc'd memory — SQLite buffers are invalidated on finalize */
        r->slug       = s0 ? strdup(s0) : NULL;
        r->title      = s1 ? strdup(s1) : NULL;
        r->content    = s2 ? strdup(s2) : NULL;
        r->tags       = s3 ? strdup(s3) : NULL;
        r->source     = s4 ? strdup(s4) : NULL;
        r->source_url = s5 ? strdup(s5) : NULL;
        r->score      = sqlite3_column_double(stmt, 6);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---------- Fact operations ---------- */
int wubu_wiki_put_fact(Wiki *w, const char *key, const char *value,
                       double confidence, const char *source) {
    if (!w || !key || !value) return 0;
    double now = (double)time(NULL);
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT OR REPLACE INTO facts (key, value, confidence, "
                      "source, created, updated) VALUES (?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, confidence);
    sqlite3_bind_text(stmt, 4, source ? source : "unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, now);
    sqlite3_bind_double(stmt, 6, now);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int wubu_wiki_get_fact(Wiki *w, const char *key, WikiFact *out) {
    if (!w || !key || !out) return 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT value, confidence, source, updated FROM facts WHERE key=?";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    const char *raw_value = (const char *)sqlite3_column_text(stmt, 0);
    const char *raw_source = (const char *)sqlite3_column_text(stmt, 2);
    /* Copy into malloc'd memory — SQLite buffers are invalidated on finalize */
    out->key        = strdup(key);
    out->value      = raw_value ? strdup(raw_value) : NULL;
    out->confidence = sqlite3_column_double(stmt, 1);
    out->source     = raw_source ? strdup(raw_source) : NULL;
    out->updated    = sqlite3_column_double(stmt, 3);
    sqlite3_finalize(stmt);
    return 1;
}

size_t wubu_wiki_list_facts(Wiki *w, WikiFact *out, size_t limit) {
    if (!w || !out || limit == 0) return 0;

    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT key, value, confidence, source, updated FROM facts "
                      "ORDER BY key LIMIT ?";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, (int)limit);

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
        WikiFact *f = &out[count];
        const char *key_val = (const char *)sqlite3_column_text(stmt, 0);
        const char *val_val = (const char *)sqlite3_column_text(stmt, 1);
        const char *src_val = (const char *)sqlite3_column_text(stmt, 3);
        /* Copy into malloc'd memory — SQLite buffers are invalidated on finalize */
        f->key        = key_val ? strdup(key_val) : NULL;
        f->value      = val_val ? strdup(val_val) : NULL;
        f->confidence = sqlite3_column_double(stmt, 2);
        f->source     = src_val ? strdup(src_val) : NULL;
        f->updated    = sqlite3_column_double(stmt, 4);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* List facts with a key prefix (e.g. "config." for config lookups) */
size_t wubu_wiki_list_facts_prefix(Wiki *w, WikiFact *out, size_t limit,
                                   const char *prefix) {
    if (!w || !out || limit == 0) return 0;

    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT key, value, confidence, source, updated FROM facts "
                      "WHERE key LIKE ? ESCAPE '\\' ORDER BY key LIMIT ?";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;

    /* Build a LIKE pattern with escaped prefix */
    char pattern[512];
    size_t p = 0;
    for (size_t i = 0; prefix && prefix[i] && p < sizeof(pattern) - 4; i++) {
        if (prefix[i] == '%' || prefix[i] == '_' || prefix[i] == '\\') {
            pattern[p++] = '\\';
        }
        pattern[p++] = prefix[i];
    }
    pattern[p++] = '%';
    pattern[p++] = '\0';

    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)limit);

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
        WikiFact *f = &out[count];
        const char *key_val = (const char *)sqlite3_column_text(stmt, 0);
        const char *val_val = (const char *)sqlite3_column_text(stmt, 1);
        const char *src_val = (const char *)sqlite3_column_text(stmt, 3);
        f->key        = key_val ? strdup(key_val) : NULL;
        f->value      = val_val ? strdup(val_val) : NULL;
        f->confidence = sqlite3_column_double(stmt, 2);
        f->source     = src_val ? strdup(src_val) : NULL;
        f->updated    = sqlite3_column_double(stmt, 4);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---------- Stats ---------- */
void wubu_wiki_stats(Wiki *w, WikiStats *out) {
    if (!w || !out) return;
    memset(out, 0, sizeof(WikiStats));

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT "
        "  (SELECT COUNT(*) FROM articles), "
        "  (SELECT COUNT(*) FROM facts), "
        "  (SELECT COUNT(*) FROM links), "
        "  (SELECT COUNT(*) FROM articles WHERE source='research'), "
        "  (SELECT COUNT(*) FROM articles WHERE source='repo')"
        ";";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->articles = (size_t)sqlite3_column_int(stmt, 0);
        out->facts    = (size_t)sqlite3_column_int(stmt, 1);
        out->links    = (size_t)sqlite3_column_int(stmt, 2);
        out->research_articles = (size_t)sqlite3_column_int(stmt, 3);
        out->repo_articles     = (size_t)sqlite3_column_int(stmt, 4);
    }
    sqlite3_finalize(stmt);
}

/* ---------- Link operations ---------- */
static int _link_exists(Wiki *w, const char *from_slug, const char *to_slug) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT 1 FROM links WHERE from_slug=? AND to_slug=? AND kind='related'";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, from_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, to_slug, -1, SQLITE_STATIC);
    rc = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
    sqlite3_finalize(stmt);
    return rc;
}

int wubu_wiki_link(Wiki *w, const char *from_slug, const char *to_slug,
                   const char *kind) {
    if (!w || !from_slug || !to_slug) return 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT OR REPLACE INTO links (from_slug, to_slug, kind) VALUES (?, ?, ?)";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, from_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, to_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, kind ? kind : "related", -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

size_t wubu_wiki_auto_links(Wiki *w) {
    if (!w) return 0;

    /* Collect all articles (slug + content) into arrays */
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT slug, content FROM articles";
    int rc = sqlite3_prepare_v2(w->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;

#define WIKI_MAX_AUTO (256)
    char  *slugs[WIKI_MAX_AUTO];
    char *contents[WIKI_MAX_AUTO];
    size_t n = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW && n < WIKI_MAX_AUTO) {
        const char *slug_val = (const char *)sqlite3_column_text(stmt, 0);
        slugs[n] = strdup(slug_val ? slug_val : "");
        const char *content_val = (const char *)sqlite3_column_text(stmt, 1);
        contents[n] = strdup(content_val ? content_val : "");
        n++;
    }
    sqlite3_finalize(stmt);


    sqlite3_exec(w->db, "BEGIN", NULL, NULL, NULL);

    size_t new_links = 0;
    for (size_t i = 0; i < n; i++) {
        /* Core name: everything after first '-', or full slug if no '-' */
        const char *dash = strchr(slugs[i], '-');
        char *core = strdup(dash ? dash + 1 : slugs[i]);
        size_t core_len = strlen(core);

        if (core_len >= 3) {
            for (size_t j = 0; j < n; j++) {
                if (i == j) continue;
                if (contents[j] && strstr(contents[j], core)) {
                    /* Article j references article i — create link j -> i */
                    if (!_link_exists(w, slugs[j], slugs[i])) {
                        wubu_wiki_link(w, slugs[j], slugs[i], "related");
                        /* Also add reverse link for bidirectional navigation */
                        if (!_link_exists(w, slugs[i], slugs[j])) {
                            wubu_wiki_link(w, slugs[i], slugs[j], "related");
                        }
                        new_links++;
                    }
                }
            }
        }
        free(core);
    }

    sqlite3_exec(w->db, "COMMIT", NULL, NULL, NULL);

    for (size_t i = 0; i < n; i++) free(slugs[i]);
    for (size_t i = 0; i < n; i++) free(contents[i]);
    return new_links;
}

void wubu_wiki_clear_links(Wiki *w) {
    if (!w) return;
    sqlite3_exec(w->db, "DELETE FROM links", NULL, NULL, NULL);
}

/* ---------- Result cleanup ---------- */
/* WikiResult string fields are strdup'd by the library (search/list/get).
 * Call this to release them after use. */
void wubu_wiki_free_result(WikiResult *r) {
    if (!r) return;
    free((void *)r->slug);
    free((void *)r->title);
    free((void *)r->content);
    free((void *)r->tags);
    free((void *)r->source);
    free((void *)r->source_url);
    r->slug = r->title = r->content = r->tags = r->source = r->source_url = NULL;
}

/* WikiFact fields are strdup'd copies allocated by wubu_wiki_get_fact
 * and wubu_wiki_list_facts. Call this to release them. */
void wubu_wiki_free_fact(WikiFact *f) {
    if (!f) return;
    free((void *)f->key);
    free((void *)f->value);
    free((void *)f->source);
    f->key = f->value = f->source = NULL;
}

/* Get the underlying sqlite3 handle (for diagnostics/external queries). */
sqlite3 *wubu_wiki_db(Wiki *w) {
    return w ? w->db : NULL;
}

/* wubu_rlm.c — Recursive Learning Memory implementation (C11, SQLite).
 *
 * Two-tier conversation memory backing the cohost AGI persona.
 * Mirrors the Python wubu_rlm.py semantics but in native C11.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_rlm.c -o wubu_rlm.o -lsqlite3 -Wall -Wextra -g
 */
#include "wubu_rlm.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ---------- Opaque struct ---------- */
struct RLMImpl {
    sqlite3 *db;
    char    *db_path;
    char    *context_slug;
    size_t   window_size;
};

/* ---------- Schema ---------- */
static const char *RLM_SCHEMA =
    "CREATE TABLE IF NOT EXISTS exchanges ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    timestamp REAL,"
    "    speaker TEXT,"
    "    text TEXT,"
    "    token_estimate INTEGER,"
    "    context_slug TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS summaries ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    timestamp REAL,"
    "    summary TEXT,"
    "    context_slug TEXT,"
    "    token_saving INTEGER,"
    "    exchange_ids TEXT"
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS summaries_fts USING fts5("
    "    summary, content='summaries', content_rowid='id'"
    ");"
    "CREATE TABLE IF NOT EXISTS facts ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    key TEXT UNIQUE,"
    "    value TEXT,"
    "    confidence REAL DEFAULT 1.0,"
    "    source TEXT,"
    "    created REAL,"
    "    updated REAL"
    ");"
    "CREATE TABLE IF NOT EXISTS context ("
    "    slug TEXT PRIMARY KEY,"
    "    title TEXT,"
    "    started REAL,"
    "    last_activity REAL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_exchanges_ctx ON exchanges(context_slug);"
    "CREATE INDEX IF NOT EXISTS idx_exchanges_ts ON exchanges(timestamp DESC);"
    "CREATE INDEX IF NOT EXISTS idx_summaries_ctx ON summaries(context_slug);"
    "CREATE INDEX IF NOT EXISTS idx_facts_key ON facts(key);"
    /* FTS5 trigger to keep summaries_fts in sync */
    "CREATE TRIGGER IF NOT EXISTS summaries_ai AFTER INSERT ON summaries BEGIN"
    "  INSERT INTO summaries_fts(rowid, summary) VALUES (new.id, new.summary);"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS summaries_ad AFTER DELETE ON summaries BEGIN"
    "  INSERT INTO summaries_fts(summaries_fts, rowid, summary) VALUES('delete', old.id, old.summary);"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS summaries_au AFTER UPDATE ON summaries BEGIN"
    "  INSERT INTO summaries_fts(summaries_fts, rowid, summary) VALUES('delete', old.id, old.summary);"
    "  INSERT INTO summaries_fts(rowid, summary) VALUES (new.id, new.summary);"
    "END;";

/* ---------- Utility ---------- */
size_t rlm_estimate_tokens(const char *text) {
    if (!text) return 1;
    /* ~4 chars per token for English */
    size_t len = strlen(text);
    return len / 4 + 1;
}

/* Heuristic summarization (no LLM needed) — mirrors Python _auto_summarize */
static char *rlm_auto_summarize(const char *buffer_text) {
    if (!buffer_text || !*buffer_text) return strdup("");

    /* Build a condensed summary: for each "Speaker: message" segment,
     * take the first ~10 words + ellipsis if longer. */
    char *copy = strdup(buffer_text);
    if (!copy) return strdup("");

    char *out = malloc(strlen(copy) * 2 + 256);
    if (!out) { free(copy); return strdup(""); }
    out[0] = '\0';

    char *line = strtok(copy, "\n");
    while (line) {
        /* Find the speaker prefix (before ':') */
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            const char *speaker = line;
            char *msg = colon + 1;
            /* Skip leading space */
            while (*msg == ' ') msg++;

            if (strlen(out) > 0) { strcat(out, "; "); }

            /* Extract first 10 words from message */
            char *msg_copy = strdup(msg);
            char *word = strtok(msg_copy, " \t");
            int word_count = 0;
            char word_buf[256];
            word_buf[0] = '\0';
            while (word && word_count < 10) {
                if (word_count > 0) { strcat(word_buf, " "); }
                strncat(word_buf, word, sizeof(word_buf) - strlen(word_buf) - 1);
                word = strtok(NULL, " \t");
                word_count++;
            }
            /* Check if there were more words */
            if (word) { strcat(word_buf, "..."); }

            /* Append "Speaker: short_msg" */
            char seg[512];
            snprintf(seg, sizeof(seg), "%s: %s", speaker, word_buf);
            strcat(out, seg);
            free(msg_copy);
        }
        line = strtok(NULL, "\n");
    }

    free(copy);

    /* Trim trailing junk */
    size_t len = strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) {
        out[--len] = '\0';
    }

    return out;
}

/* ---------- Lifecycle ---------- */
RLM *wubu_rlm_open(const char *db_path, const char *context_slug) {
    if (!db_path || !context_slug) return NULL;
    RLM *rlm = (RLM *)calloc(1, sizeof(RLM));
    if (!rlm) return NULL;
    rlm->db_path = strdup(db_path);
    rlm->context_slug = strdup(context_slug);
    rlm->window_size = RLM_WINDOW_SIZE;
    if (!rlm->db_path || !rlm->context_slug) {
        free(rlm->db_path);
        free(rlm->context_slug);
        free(rlm);
        return NULL;
    }

    int rc = sqlite3_open_v2(db_path, &rlm->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_rlm: cannot open %s: %s\n",
                db_path, sqlite3_errmsg(rlm->db));
        sqlite3_close(rlm->db);
        free(rlm->db_path);
        free(rlm->context_slug);
        free(rlm);
        return NULL;
    }

    /* Enable WAL */
    char *err = NULL;
    sqlite3_exec(rlm->db,
        "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;",
        NULL, NULL, &err);
    sqlite3_free(err);

    /* Create schema */
    rc = sqlite3_exec(rlm->db, RLM_SCHEMA, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_rlm: schema error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(rlm->db);
        free(rlm->db_path);
        free(rlm->context_slug);
        free(rlm);
        return NULL;
    }

    /* Initialize/update context */
    double now = (double)time(NULL);
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(rlm->db,
        "INSERT OR REPLACE INTO context (slug, title, started, last_activity) "
        "VALUES (?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, context_slug, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, context_slug, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 3, now);
        sqlite3_bind_double(stmt, 4, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return rlm;
}

void wubu_rlm_close(RLM *rlm) {
    if (!rlm) return;
    if (rlm->db) sqlite3_close(rlm->db);
    free(rlm->db_path);
    free(rlm->context_slug);
    free(rlm);
}

/* ---------- Add exchange ---------- */
int wubu_rlm_add_exchange(RLM *rlm, const char *speaker, const char *text,
                          rlm_summary_fn summary_fn, void *summary_ud,
                          size_t *token_estimate) {
    if (!rlm || !speaker || !text) return 0;

    double now = (double)time(NULL);
    size_t tokens = rlm_estimate_tokens(text);
    if (token_estimate) *token_estimate = tokens;

    sqlite3_stmt *stmt = NULL;
    int rc;

    /* Insert exchange */
    rc = sqlite3_prepare_v2(rlm->db,
        "INSERT INTO exchanges (timestamp, speaker, text, token_estimate, context_slug) "
        "VALUES (?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_double(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, speaker, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, (int)tokens);
    sqlite3_bind_text(stmt, 5, rlm->context_slug, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* Update context last_activity */
    rc = sqlite3_prepare_v2(rlm->db,
        "UPDATE context SET last_activity=? WHERE slug=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, now);
        sqlite3_bind_text(stmt, 2, rlm->context_slug, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    /* Check if buffer exceeds threshold */
    rc = sqlite3_prepare_v2(rlm->db,
        "SELECT COALESCE(SUM(token_estimate), 0) FROM exchanges WHERE context_slug=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);
    int total_tokens = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total_tokens = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (total_tokens <= RLM_SUMMARY_THRESHOLD) {
        return 0; /* Not summarized */
    }

    /* Summarize the buffer */
    rc = sqlite3_prepare_v2(rlm->db,
        "SELECT id, speaker, text FROM exchanges WHERE context_slug=? "
        "ORDER BY timestamp ASC", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);

    /* Collect exchanges into buffer text for summarization */
    char *buffer = malloc(65536);  /* generous buffer */
    if (!buffer) { sqlite3_finalize(stmt); return 0; }
    buffer[0] = '\0';
    char *exchange_ids = malloc(1024);
    if (!exchange_ids) { free(buffer); sqlite3_finalize(stmt); return 0; }
    exchange_ids[0] = '\0';
    size_t buf_used = 0;
    size_t ids_used = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *spk = (const char *)sqlite3_column_text(stmt, 1);
        const char *txt = (const char *)sqlite3_column_text(stmt, 2);

        /* Append to buffer */
        int written = snprintf(buffer + buf_used, 65536 - buf_used,
                               "%s: %s\n", spk ? spk : "unknown", txt ? txt : "");
        if (written > 0) buf_used += (size_t)written;

        /* Append to IDs */
        if (ids_used > 0) {
            exchange_ids[ids_used++] = ',';
        }
        int id_written = snprintf(exchange_ids + ids_used, 1024 - ids_used,
                                  "%d", id);
        if (id_written > 0) ids_used += (size_t)id_written;
    }
    sqlite3_finalize(stmt);

    /* Generate summary */
    char *summary = NULL;
    if (summary_fn) {
        summary = summary_fn(buffer, summary_ud);
    } else {
        summary = rlm_auto_summarize(buffer);
    }

    /* Store summary */
    rc = sqlite3_prepare_v2(rlm->db,
        "INSERT INTO summaries (timestamp, summary, context_slug, token_saving, exchange_ids) "
        "VALUES (?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, now);
        sqlite3_bind_text(stmt, 2, summary ? summary : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, rlm->context_slug, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, (int)buf_used); /* token estimate */
        sqlite3_bind_text(stmt, 5, exchange_ids, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    /* Clear exchanges (long-term now in summary) */
    rc = sqlite3_prepare_v2(rlm->db,
        "DELETE FROM exchanges WHERE context_slug=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    /* Trim old summaries beyond MAX_SUMMARIES */
    rc = sqlite3_prepare_v2(rlm->db,
        "SELECT id FROM summaries WHERE context_slug=? "
        "ORDER BY timestamp DESC LIMIT -1 OFFSET ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, RLM_MAX_SUMMARIES);
        int ids[100];
        int nid = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && nid < 100) {
            ids[nid++] = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        if (nid > 0) {
            char placeholders[8192] = "";
            for (int i = 0; i < nid; i++) {
                if (i > 0) strcat(placeholders, ",");
                strcat(placeholders, "?");
            }
            char del_sql[8400];
            snprintf(del_sql, sizeof(del_sql),
                     "DELETE FROM summaries WHERE id IN (%s)", placeholders);
            rc = sqlite3_prepare_v2(rlm->db, del_sql, -1, &stmt, NULL);
            if (rc == SQLITE_OK) {
                for (int i = 0; i < nid; i++) {
                    sqlite3_bind_int(stmt, i + 1, ids[i]);
                }
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    free(buffer);
    free(exchange_ids);
    free(summary);

    return 1; /* Summarized */
}

/* ---------- Get context ---------- */
size_t wubu_rlm_get_context(RLM *rlm, const char *speaker_out[],
                            const char *text_out[], size_t limit) {
    if (!rlm || !speaker_out || !text_out || limit == 0) return 0;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(rlm->db,
        "SELECT speaker, text FROM exchanges WHERE context_slug=? "
        "ORDER BY timestamp DESC LIMIT ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, (int)limit);

    /* Collect into temp array (reversed for chronological order) */
    size_t count = 0;
    const char *speakers[RLM_WINDOW_SIZE];
    const char *texts[RLM_WINDOW_SIZE];
    while (sqlite3_step(stmt) == SQLITE_ROW && count < RLM_WINDOW_SIZE) {
        speakers[count] = strdup((const char *)sqlite3_column_text(stmt, 0));
        texts[count] = strdup((const char *)sqlite3_column_text(stmt, 1));
        count++;
    }
    sqlite3_finalize(stmt);

    /* Reverse to get chronological order */
    for (size_t i = 0; i < count; i++) {
        speaker_out[i] = speakers[count - 1 - i];
        text_out[i] = texts[count - 1 - i];
    }

    return count;
}

/* ---------- Recall ---------- */
size_t wubu_rlm_recall(RLM *rlm, const char *query, RLMRecall *out, size_t limit) {
    if (!rlm || !query || !out || limit == 0) return 0;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(rlm->db,
        "SELECT summary, context_slug, timestamp, token_saving "
        "FROM summaries WHERE summaries_fts MATCH ? "
        "ORDER BY rank LIMIT ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        /* FTS5 query: prefix search */
        sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, (int)limit);
    } else {
        /* Fallback: LIKE-based search */
        sqlite3_finalize(stmt);
        rc = sqlite3_prepare_v2(rlm->db,
            "SELECT summary, context_slug, timestamp, token_saving "
            "FROM summaries WHERE summary LIKE ? ORDER BY timestamp DESC LIMIT ?",
            -1, &stmt, NULL);
        if (rc != SQLITE_OK) return 0;
        char like_pattern[512];
        snprintf(like_pattern, sizeof(like_pattern), "%%%s%%", query);
        sqlite3_bind_text(stmt, 1, like_pattern, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, (int)limit);
    }

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
        const char *s = (const char *)sqlite3_column_text(stmt, 0);
        const char *c = (const char *)sqlite3_column_text(stmt, 1);
        out[count].summary = s ? strdup(s) : NULL;
        out[count].context_slug = c ? strdup(c) : NULL;
        out[count].timestamp = sqlite3_column_double(stmt, 2);
        out[count].tokens_saved = sqlite3_column_int(stmt, 3);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---------- Fact storage ---------- */
int wubu_rlm_store_fact(RLM *rlm, const char *key, const char *value,
                        double confidence, const char *source) {
    if (!rlm || !key || !value) return 0;
    double now = (double)time(NULL);
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(rlm->db,
        "INSERT OR REPLACE INTO facts (key, value, confidence, source, created, updated) "
        "VALUES (?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
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

int wubu_rlm_get_fact(RLM *rlm, const char *key, RLMFact *out) {
    if (!rlm || !key || !out) return 0;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(rlm->db,
        "SELECT value, confidence, source, updated FROM facts WHERE key=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    const char *val = (const char *)sqlite3_column_text(stmt, 0);
    const char *src = (const char *)sqlite3_column_text(stmt, 2);
    out->value = val ? strdup(val) : NULL;
    out->confidence = sqlite3_column_double(stmt, 1);
    out->source = src ? strdup(src) : NULL;
    out->updated = sqlite3_column_double(stmt, 3);
    sqlite3_finalize(stmt);
    return 1;
}

/* ---------- Statistics ---------- */
void wubu_rlm_stats(RLM *rlm, RLMStats *out) {
    if (!rlm || !out) return;
    memset(out, 0, sizeof(RLMStats));

    sqlite3_stmt *stmt = NULL;

    /* Exchanges for this context */
    int rc = sqlite3_prepare_v2(rlm->db,
        "SELECT COUNT(*), COALESCE(SUM(token_estimate), 0) FROM exchanges "
        "WHERE context_slug=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out->short_exchanges = (size_t)sqlite3_column_int(stmt, 0);
            out->short_tokens = (size_t)sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    /* Summaries for this context */
    rc = sqlite3_prepare_v2(rlm->db,
        "SELECT COUNT(*), COALESCE(SUM(token_saving), 0) FROM summaries "
        "WHERE context_slug=?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rlm->context_slug, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out->long_summaries = (size_t)sqlite3_column_int(stmt, 0);
            out->long_tokens_saved = (size_t)sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    /* Total facts */
    rc = sqlite3_prepare_v2(rlm->db,
        "SELECT COUNT(*) FROM facts", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out->facts = (size_t)sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    out->context = rlm->context_slug;
    out->db_path = rlm->db_path;
}

/* ---------- Cleanup ---------- */
void wubu_rlm_free_recall(RLMRecall *r) {
    if (!r) return;
    free((void *)r->summary);
    free((void *)r->context_slug);
    r->summary = r->context_slug = NULL;
}

void wubu_rlm_free_fact(RLMFact *f) {
    if (!f) return;
    free((void *)f->value);
    free((void *)f->source);
    f->value = f->source = NULL;
}

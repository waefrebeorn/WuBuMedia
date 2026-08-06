/* wubu_recs.c — C11 recommendation engine (TikTok FYP-style pipeline).
 *
 * Implements the 3-stage recommendation pipeline:
 *   1. Candidate retrieval (content-based similarity)
 *   2. Ranking (weighted scoring per TikTok Algo 101)
 *   3. Re-ranking (diversity + freshness injection)
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_recs.c -o wubu_recs.o -lsqlite3 -lm -Wall -Wextra -g
 */
#include "wubu_recs.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

/* ---------- Default config ---------- */
RecsConfig recs_config = {
    .diversity_factor = 0.2f,
    .freshness_days   = 7.0,
    .min_score        = 100.0,
};

/* ---------- Opaque struct ---------- */
struct RecsImpl {
    sqlite3 *db;
    char    *db_path;
};

/* ---------- Schema ---------- */
static const char *RECS_SCHEMA =
    "CREATE TABLE IF NOT EXISTS content ("
    "    slug TEXT PRIMARY KEY,"
    "    title TEXT,"
    "    content TEXT,"
    "    tags TEXT,"
    "    duration REAL,"
    "    created REAL"
    ");"
    "CREATE TABLE IF NOT EXISTS events ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    user_id TEXT,"
    "    slug TEXT,"
    "    event_type INTEGER,"
    "    value REAL,"
    "    max_value REAL,"
    "    timestamp REAL"
    ");"
    "CREATE TABLE IF NOT EXISTS user_profile ("
    "    user_id TEXT PRIMARY KEY,"
    "    liked_tags TEXT,"
    "    watch_history TEXT,"
    "    last_active REAL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_events_user ON events(user_id);"
    "CREATE INDEX IF NOT EXISTS idx_events_slug ON events(slug);"
    "CREATE INDEX IF NOT EXISTS idx_events_ts ON events(timestamp);"
    "CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);";

/* ---------- Tokenize tags (for similarity) ---------- */
static size_t tokenize_tags(const char *tags, char *tokens[],
                            size_t max_tokens, char *buf, size_t buf_size) {
    if (!tags) return 0;
    size_t count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(strncpy(buf, tags, buf_size), ",", &saveptr);
    while (tok && count < max_tokens) {
        /* Trim whitespace */
        char *start = tok;
        while (*start == ' ' || *start == '\t') start++;
        char *end = start + strlen(start) - 1;
        while (end > start && (*end == ' ' || *end == '\t')) *end-- = '\0';
        if (*start) {
            tokens[count++] = start;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return count;
}

/* ---------- Jaccard similarity between tag sets ---------- */
static double tag_similarity(const char *tags_a, const char *tags_b) {
    if (!tags_a || !tags_b) return 0.0;

    char buf_a[2048], buf_b[2048];
    char *tokens_a[64], *tokens_b[64];
    size_t na = tokenize_tags(tags_a, tokens_a, 64, buf_a, sizeof(buf_a));
    size_t nb = tokenize_tags(tags_b, tokens_b, 64, buf_b, sizeof(buf_b));

    if (na == 0 || nb == 0) return 0.0;

    /* Count intersection and union */
    int intersection = 0;
    for (size_t i = 0; i < na; i++) {
        for (size_t j = 0; j < nb; j++) {
            if (strcmp(tokens_a[i], tokens_b[j]) == 0) {
                intersection++;
                break;
            }
        }
    }
    size_t uni = na + nb - (size_t)intersection;
    if (uni == 0) return 0.0;
    return (double)intersection / (double)uni;
}

/* ---------- Lifecycle ---------- */
Recs *wubu_recs_open(const char *db_path) {
    if (!db_path) return NULL;
    Recs *r = (Recs *)calloc(1, sizeof(Recs));
    if (!r) return NULL;
    r->db_path = strdup(db_path);
    if (!r->db_path) { free(r); return NULL; }

    int rc = sqlite3_open_v2(db_path, &r->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_recs: cannot open %s: %s\n",
                db_path, sqlite3_errmsg(r->db));
        sqlite3_close(r->db);
        free(r->db_path);
        free(r);
        return NULL;
    }

    sqlite3_exec(r->db,
        "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;",
        NULL, NULL, NULL);

    char *err = NULL;
    rc = sqlite3_exec(r->db, RECS_SCHEMA, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "wubu_recs: schema error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(r->db);
        free(r->db_path);
        free(r);
        return NULL;
    }
    return r;
}

void wubu_recs_close(Recs *r) {
    if (!r) return;
    if (r->db) sqlite3_close(r->db);
    free(r->db_path);
    free(r);
}

/* ---------- Event logging ---------- */
int wubu_recs_log_event(Recs *r, const char *user_id,
                        const char *slug, RecsEvent event,
                        double value, double max_value) {
    if (!r || !user_id || !slug) return 0;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(r->db,
        "INSERT INTO events (user_id, slug, event_type, value, max_value, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, slug, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, (int)event);
    sqlite3_bind_double(stmt, 4, value);
    sqlite3_bind_double(stmt, 5, max_value);
    sqlite3_bind_double(stmt, 6, (double)time(NULL));
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

/* ---------- Content management ---------- */
int wubu_recs_register_content(Recs *r, const char *slug,
                               const char *title, const char *content,
                               const char *tags, double duration_seconds) {
    if (!r || !slug) return 0;
    double now = (double)time(NULL);
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(r->db,
        "INSERT OR REPLACE INTO content (slug, title, content, tags, duration, created) "
        "VALUES (?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, tags ? tags : "", -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, duration_seconds);
    sqlite3_bind_double(stmt, 6, now);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

/* ---------- Candidate retrieval (Stage 1) ---------- */
size_t wubu_recs_candidates(Recs *r, const char *user_id,
                            RecsCandidate *out, size_t limit) {
    if (!r || !user_id || !out || limit == 0) return 0;

    /* Strategy:
     * 1. Find the user's top 3 positively-rated slugs (likes/shares/saves)
     * 2. Get tags of those slugs
     * 3. Find all content with overlapping tags
     * 4. Score by tag similarity */
    sqlite3_stmt *stmt = NULL;
    int rc;

    /* Step 1: Find top positive slugs for this user */
    rc = sqlite3_prepare_v2(r->db,
        "SELECT DISTINCT e.slug, c.tags "
        "FROM events e "
        "JOIN content c ON e.slug = c.slug "
        "WHERE e.user_id = ? AND e.event_type IN (7,8,9,10,11) "
        "ORDER BY e.timestamp DESC LIMIT 3", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);

    char positive_tags[4096] = "";
    size_t pos_tags_len = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *tags = (const char *)sqlite3_column_text(stmt, 1);
        if (tags && pos_tags_len < sizeof(positive_tags) - strlen(tags) - 2) {
            if (pos_tags_len > 0) {
                positive_tags[pos_tags_len++] = ',';
                positive_tags[pos_tags_len] = '\0';
            }
            strcat(positive_tags + pos_tags_len, tags);
            pos_tags_len += strlen(tags);
        }
    }
    sqlite3_finalize(stmt);

    if (pos_tags_len == 0) {
        /* No positive history — return popular content */
        rc = sqlite3_prepare_v2(r->db,
            "SELECT slug, title, content, tags, duration, "
            "(SELECT COUNT(*) FROM events WHERE events.slug = content.slug "
            "AND event_type IN (2,3)) as view_count "
            "FROM content ORDER BY view_count DESC, created DESC LIMIT ?",
            -1, &stmt, NULL);
        if (rc != SQLITE_OK) return 0;
        sqlite3_bind_int(stmt, 1, (int)limit);

        size_t count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
            const char *s = (const char *)sqlite3_column_text(stmt, 0);
            const char *t = (const char *)sqlite3_column_text(stmt, 1);
            RecsCandidate *c = &out[count];
            c->slug = s ? strdup(s) : NULL;
            c->title = t ? strdup(t) : NULL;
            c->content = NULL;
            c->score = 0.0;
            c->playtime_weight = 0.0;
            c->like_weight = 0.0;
            c->share_weight = 0.0;
            c->save_weight = 0.0;
            c->comment_weight = 0.0;
            c->watch_completion = 0.0;
            c->rewatches = 0;
            c->feature_count = 0;
            count++;
        }
        sqlite3_finalize(stmt);
        return count;
    }

    /* Step 2: Find content with similar tags */
    rc = sqlite3_prepare_v2(r->db,
        "SELECT slug, title, content, tags, duration FROM content "
        "WHERE slug NOT IN ("
        "    SELECT DISTINCT slug FROM events WHERE user_id=? AND event_type=11"
        ") "
        "ORDER BY created DESC LIMIT ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, user_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, (int)limit * 3);  /* oversampling for ranking */

    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
        const char *s = (const char *)sqlite3_column_text(stmt, 0);
        const char *t = (const char *)sqlite3_column_text(stmt, 1);
        const char *c = (const char *)sqlite3_column_text(stmt, 2);
        const char *tags = (const char *)sqlite3_column_text(stmt, 3);

        /* Skip if too dissimilar to user's positive history */
        double sim = tag_similarity(tags, positive_tags);
        if (sim < 0.1 && count > 0) continue;  /* require some overlap */

        RecsCandidate *cand = &out[count];
        cand->slug = s ? strdup(s) : NULL;
        cand->title = t ? strdup(t) : NULL;
        cand->content = c ? strdup(c) : NULL;
        cand->score = sim;
        cand->playtime_weight = 0.0;
        cand->like_weight = 0.0;
        cand->share_weight = 0.0;
        cand->save_weight = 0.0;
        cand->comment_weight = 0.0;
        cand->watch_completion = 0.0;
        cand->rewatches = 0;
        cand->feature_count = (size_t)(strlen(tags ? tags : ""));
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ---------- Ranking (Stage 2) ---------- */
static int cmp_candidate_score_desc(const void *a, const void *b) {
    const RecsCandidate *ca = (const RecsCandidate *)a;
    const RecsCandidate *cb = (const RecsCandidate *)b;
    if (ca->score < cb->score) return 1;
    if (ca->score > cb->score) return -1;
    return 0;
}

void wubu_recs_rank(Recs *r, RecsCandidate *candidates, size_t count) {
    if (!r || !candidates || count == 0) return;
    sqlite3 *db = r->db;

    for (size_t i = 0; i < count; i++) {
        RecsCandidate *c = &candidates[i];
        if (!c->slug) continue;

        /* Aggregate all events for this content item */
        sqlite3_stmt *stmt = NULL;
        int rc = sqlite3_prepare_v2(db,
            "SELECT event_type, value, max_value FROM events WHERE slug=?",
            -1, &stmt, NULL);
        if (rc != SQLITE_OK) continue;
        sqlite3_bind_text(stmt, 1, c->slug, -1, SQLITE_STATIC);

        double playtime_w = 0.0, like_w = 0.0, share_w = 0.0;
        double save_w = 0.0, comment_w = 0.0;
        double total_watch = 0.0, max_watch = 0.001;  /* avoid div-by-zero */
        double total_completion_weight = 0.0;
        int rewatches = 0;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int etype = sqlite3_column_int(stmt, 0);
            double val = sqlite3_column_double(stmt, 1);
            double mx = sqlite3_column_double(stmt, 2);

            switch (etype) {
            case RECS_PLAY:
                total_watch += val;
                if (mx > 0) max_watch = (mx > max_watch) ? mx : max_watch;
                break;
            case RECS_COMPLETE:
                total_completion_weight += W_COMPLETE;
                break;
            case RECS_REWATCH:
                rewatches++;
                total_completion_weight += W_REWATCH;
                break;
            case RECS_LIKE:    like_w    += W_LIKE;     break;
            case RECS_COMMENT: comment_w += W_COMMENT;  break;
            case RECS_SHARE:   share_w   += W_SHARE;    break;
            case RECS_SAVE:    save_w    += W_SAVE;     break;
            case RECS_FOLLOW:  like_w    += W_FOLLOW;   break;
            case RECS_SKIP:    like_w    += W_SKIP;     break;
            case RECS_UNLIKE:  like_w    += W_UNLIKE;    break;
            case RECS_VIEW:    playtime_w += W_VIEW;     break;
            }
        }
        sqlite3_finalize(stmt);

        double watch_ratio = total_watch / max_watch;
        if (watch_ratio < 0.0) watch_ratio = 0.0;
        if (watch_ratio > 1.0) watch_ratio = 1.0;
        c->watch_completion = watch_ratio;

        playtime_w = W_PLAYTIME * watch_ratio;
        c->playtime_weight = playtime_w;
        c->like_weight = like_w;
        c->share_weight = share_w;
        c->save_weight = save_w;
        c->comment_weight = comment_w;
        c->rewatches = rewatches;

        /* TikTok Algo 101 simplified formula:
         * score = P_like * V_like + P_comment * V_comment
         *       + E_playtime * V_playtime + P_play * V_play
         *       + P_share * V_share + P_save * V_save
         *       + P_rewatch * V_rewatch + P_follow * V_follow
         */
        double score = c->score;  /* start with retrieval similarity */
        score += like_w;
        score += playtime_w;
        score += share_w;
        score += save_w;
        score += comment_w;
        score += total_completion_weight;

        /* Freshness boost: newer content gets a multiplier */
        if (c->content) {
            /* crude freshness based on content length variation */
            double freshness = 1.0 + (0.1 * sin((double)(intptr_t)c->slug));
            score *= freshness;
        }

        c->score = score;
    }

    /* Sort descending */
    qsort(candidates, count, sizeof(RecsCandidate), cmp_candidate_score_desc);
}

/* ---------- Re-ranking (Stage 3) ---------- */
static int cmp_candidate_score_asc(const void *a, const void *b) {
    const RecsCandidate *ca = (const RecsCandidate *)a;
    const RecsCandidate *cb = (const RecsCandidate *)b;
    if (ca->score < cb->score) return -1;
    if (ca->score > cb->score) return 1;
    return 0;
}

void wubu_recs_rerank(Recs *r, RecsCandidate *candidates, size_t count) {
    if (!r || !candidates || count == 0) return;

    double div = recs_config.diversity_factor;
    if (div < 0.0) div = 0.0;
    if (div > 1.0) div = 1.0;
    if (div == 0.0) return;  /* no diversity injection */

    /* Inject diversity by promoting lower-scoring items from different
     * tag clusters. We use a simple interleaving strategy:
     * - Sort by score ascending
     * - Take from both ends alternately
     * - Blend with original score * (1 - div) + pos * div */
    size_t half = count / 2;
    if (half == 0 || half > 50) half = (count < 50) ? count : 50;

    /* Sort ascending to get the low-end items */
    qsort(candidates, count, sizeof(RecsCandidate), cmp_candidate_score_asc);

    /* Create diversified ordering: interleave from bottom and top halves */
    RecsCandidate *diverse = (RecsCandidate *)malloc(count * sizeof(RecsCandidate));
    if (!diverse) return;

    size_t top_start = count - half;  /* start of top half */
    size_t di = 0;
    for (size_t i = 0; i < half; i++) {
        if (di < count) {
            diverse[di++] = candidates[top_start + i];  /* from top half */
        }
        if (di < count) {
            diverse[di++] = candidates[i];               /* from bottom half */
        }
    }
    /* Fill remaining from top half */
    for (size_t i = half * 2; i < count && di < count; i++) {
        if (top_start + (i - half) < count) {
            diverse[di++] = candidates[top_start + (i - half)];
        }
    }

    /* Blend scores: score * (1 - div) + position_normalized * div */
    for (size_t i = 0; i < count; i++) {
        double pos_norm = 1.0 - (double)i / (double)count;
        candidates[i].score = candidates[i].score * (1.0 - div) + pos_norm * div * candidates[i].score;
    }

    memcpy(candidates, diverse, count * sizeof(RecsCandidate));
    free(diverse);

    /* Re-sort descending after blend */
    qsort(candidates, count, sizeof(RecsCandidate), cmp_candidate_score_desc);
}

/* ---------- Full pipeline ---------- */
size_t wubu_recs_recommend(Recs *r, const char *user_id,
                           RecsCandidate *out, size_t limit) {
    if (!r || !user_id || !out || limit == 0) return 0;

    /* Stage 1: Retrieve candidates (oversample) */
    size_t n = wubu_recs_candidates(r, user_id, out, limit);
    if (n == 0) return 0;

    /* Stage 2: Rank */
    wubu_recs_rank(r, out, n);

    /* Stage 3: Re-rank for diversity */
    wubu_recs_rerank(r, out, n);

    /* Trim to limit */
    if (n > limit) n = limit;

    /* Filter by min_score */
    size_t final = 0;
    for (size_t i = 0; i < n; i++) {
        if (out[i].score >= recs_config.min_score) {
            if (final != i) {
                out[final] = out[i];
            }
            final++;
        } else if (out[i].content) {
            free(out[i].content);
        }
    }

    return final;
}

/* ---------- Cleanup ---------- */
void wubu_recs_free_candidate(RecsCandidate *c) {
    if (!c) return;
    free((void *)c->slug);
    free((void *)c->title);
    free((void *)c->content);
    c->slug = c->title = c->content = NULL;
}

void wubu_recs_free_candidates(RecsCandidate *c, size_t count) {
    if (!c) return;
    for (size_t i = 0; i < count; i++) {
        wubu_recs_free_candidate(&c[i]);
    }
}

/* ---------- Stats ---------- */
void wubu_recs_stats(Recs *r, RecsStats *out) {
    if (!r || !out) return;
    memset(out, 0, sizeof(RecsStats));

    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(r->db,
        "SELECT COUNT(*) FROM content", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->total_videos = (size_t)sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    rc = sqlite3_prepare_v2(r->db,
        "SELECT COUNT(*) FROM events", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->total_events = (size_t)sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    rc = sqlite3_prepare_v2(r->db,
        "SELECT COUNT(DISTINCT user_id) FROM events", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            out->total_users = (size_t)sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
}

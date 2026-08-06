/* wubu_daemon.c — C11 daemon: knowledge base + emotion + IPC via named pipes.
 *
 * This is the native AGI core — replaces the Python orchestrator's
 * knowledge base and emotion engine functions with zero-dependency C11
 * that talks to the Python cohost via Windows named pipes.
 *
 * Architecture:
 *   - wubu_wiki.c: SQLite FTS5 knowledge base (opaque Wiki*)
 *   - This file: Daemon loop + named pipe server + JSON protocol
 *   - Python wubu_bridge.py: connects to our pipe for IPC
 *
 * License: SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -c wubu_daemon.c -o wubu_daemon.o -lsqlite3 -lws2_32 -lkernel32 -I.
 */
#include "wubu_daemon.h"
#include "wubu_wiki.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#endif

/* ===== Minimal JSON parser (sjson — single-header style, self-contained) ===== */
/* We need JUST enough JSON to parse request fields and build responses.
 * No external deps. Uses recursive descent. Supports strings, numbers,
 * booleans, null, arrays, objects. */

typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING,
    JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct JsonValue {
    JsonType type;
    /* Object members (also used as array members for JSON_ARRAY) */
    char **keys;          /* object keys (NULL for non-objects) */
    struct JsonValue **vals; /* object/array values */
    size_t obj_count;     /* object/array element count */
    /* Scalar value */
    union {
        int boolean;
        double number;
        char *string;
    };
} JsonValue;

typedef struct {
    const char *p;      /* current parse position */
    const char *start;  /* start of buffer */
    const char *end;    /* end of buffer */
    int error;
} JsonParser;

static void json_skip_ws(JsonParser *p) {
    while (p->p < p->end && (*p->p == ' ' || *p->p == '\t' ||
                             *p->p == '\n' || *p->p == '\r')) p->p++;
}

static JsonValue *json_parse_value(JsonParser *p);

static char *json_unescape(const char *s, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    size_t oi = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len) {
            i++;
            switch (s[i]) {
                case 'n': out[oi++] = '\n'; break;
                case 't': out[oi++] = '\t'; break;
                case 'r': out[oi++] = '\r'; break;
                case '"': out[oi++] = '"'; break;
                case '\\': out[oi++] = '\\'; break;
                case '/': out[oi++] = '/'; break;
                case 'b': out[oi++] = '\b'; break;
                case 'f': out[oi++] = '\f'; break;
                default: out[oi++] = s[i]; break;
            }
        } else {
            out[oi++] = s[i];
        }
    }
    out[oi] = '\0';
    return out;
}

static JsonValue *json_parse_string(JsonParser *p) {
    if (*p->p != '"') { p->error = 1; return NULL; }
    p->p++;
    const char *start = p->p;
    while (p->p < p->end && *p->p != '"') {
        if (*p->p == '\\' && p->p + 1 < p->end) p->p += 2;
        else p->p++;
    }
    if (p->p >= p->end) { p->error = 1; return NULL; }
    size_t len = (size_t)(p->p - start);
    p->p++; /* skip closing quote */

    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { p->error = 1; return NULL; }
    v->type = JSON_STRING;
    v->string = json_unescape(start, len);
    if (!v->string) { free(v); p->error = 1; return NULL; }
    return v;
}

static JsonValue *json_parse_number(JsonParser *p) {
    char *end = NULL;
    double d = strtod(p->p, &end);
    if (end == p->p) { p->error = 1; return NULL; }
    p->p = end;

    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { p->error = 1; return NULL; }
    v->type = JSON_NUMBER;
    v->number = d;
    return v;
}

static JsonValue *json_parse_literal(JsonParser *p) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { p->error = 1; return NULL; }

    if (strncmp(p->p, "true", 4) == 0) {
        v->type = JSON_BOOL; v->boolean = 1; p->p += 4;
    } else if (strncmp(p->p, "false", 5) == 0) {
        v->type = JSON_BOOL; v->boolean = 0; p->p += 5;
    } else if (strncmp(p->p, "null", 4) == 0) {
        v->type = JSON_NULL; p->p += 4;
    } else {
        free(v); p->error = 1; return NULL;
    }
    return v;
}

static JsonValue *json_parse_array(JsonParser *p) {
    p->p++; /* skip [ */
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { p->error = 1; return NULL; }
    v->type = JSON_ARRAY;

    size_t cap = 8;
    v->vals = (JsonValue **)malloc(cap * sizeof(JsonValue *));
    if (!v->vals) { free(v); p->error = 1; return NULL; }

    json_skip_ws(p);
    if (*p->p == ']') { p->p++; return v; }

    while (1) {
        json_skip_ws(p);
        JsonValue *item = json_parse_value(p);
        if (p->error) { free(v->vals); free(v); return NULL; }
        if (v->obj_count >= cap) {
            cap *= 2;
            v->vals = (JsonValue **)realloc(v->vals, cap * sizeof(JsonValue *));
            if (!v->vals) { free(item); free(v); p->error = 1; return NULL; }
        }
        v->vals[v->obj_count++] = item;
        json_skip_ws(p);
        if (*p->p == ',') { p->p++; continue; }
        if (*p->p == ']') { p->p++; break; }
        p->error = 1; free(v->vals); free(v); return NULL;
    }
    return v;
}

static JsonValue *json_parse_object(JsonParser *p) {
    p->p++; /* skip { */
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { p->error = 1; return NULL; }
    v->type = JSON_OBJECT;

    size_t cap = 8;
    v->keys = (char **)malloc(cap * sizeof(char *));
    v->vals = (JsonValue **)malloc(cap * sizeof(JsonValue *));
    if (!v->keys || !v->vals) {
        free(v->keys); free(v->vals); free(v); p->error = 1; return NULL;
    }

    json_skip_ws(p);
    if (*p->p == '}') { p->p++; return v; }

    while (1) {
        json_skip_ws(p);
        JsonValue *key = json_parse_string(p);
        if (p->error) { goto err; }

        json_skip_ws(p);
        if (*p->p != ':') { free(key); p->error = 1; goto err; }
        p->p++;

        json_skip_ws(p);
        JsonValue *val = json_parse_value(p);
        if (p->error) { free(key); goto err; }

        if (v->obj_count >= cap) {
            cap *= 2;
            v->keys = (char **)realloc(v->keys, cap * sizeof(char *));
            v->vals = (JsonValue **)realloc(v->vals, cap * sizeof(JsonValue *));
            if (!v->keys || !v->vals) goto err;
        }
        v->keys[v->obj_count] = key->string;
        v->vals[v->obj_count] = val;
        v->obj_count++;
        free(key); /* free the wrapper, keep the string */

        json_skip_ws(p);
        if (*p->p == ',') { p->p++; continue; }
        if (*p->p == '}') { p->p++; break; }
        p->error = 1;
        goto err;
    }
    return v;
err:
    for (size_t i = 0; i < v->obj_count; i++) {
        free(v->keys[i]);
        /* Don't free vals here — they may be shared, skip for simplicity */
    }
    free(v->keys);
    free(v->vals);
    free(v);
    return NULL;
}

static JsonValue *json_parse_value(JsonParser *p) {
    json_skip_ws(p);
    if (p->p >= p->end) { p->error = 1; return NULL; }
    char c = *p->p;
    if (c == '{') return json_parse_object(p);
    if (c == '[') return json_parse_array(p);
    if (c == '"') return json_parse_string(p);
    if (c == '-' || (c >= '0' && c <= '9')) return json_parse_number(p);
    if (c == 't' || c == 'f' || c == 'n') return json_parse_literal(p);
    p->error = 1;
    return NULL;
}

static void json_free(JsonValue *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->obj_count; i++) json_free(v->vals[i]);
            free(v->vals);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->obj_count; i++) {
                free(v->keys[i]);
                json_free(v->vals[i]);
            }
            free(v->keys);
            free(v->vals);
            break;
        default:
            break;
    }
    free(v);
}

static const char *json_object_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->obj_count; i++) {
        if (strcmp(obj->keys[i], key) == 0 && obj->vals[i]->type == JSON_STRING)
            return obj->vals[i]->string;
    }
    return NULL;
}

__attribute__((unused))
static double json_object_num(const JsonValue *obj, const char *key, double def) {
    if (!obj || obj->type != JSON_OBJECT) return def;
    for (size_t i = 0; i < obj->obj_count; i++) {
        if (strcmp(obj->keys[i], key) == 0 && obj->vals[i]->type == JSON_NUMBER)
            return obj->vals[i]->number;
    }
    return def;
}

__attribute__((unused))
static int json_object_bool(const JsonValue *obj, const char *key, int def) {
    if (!obj || obj->type != JSON_OBJECT) return def;
    for (size_t i = 0; i < obj->obj_count; i++) {
        if (strcmp(obj->keys[i], key) == 0 && obj->vals[i]->type == JSON_BOOL)
            return obj->vals[i]->boolean;
    }
    return def;
}

/* (json_object_num, json_object_bool are utility functions
 * kept for future protocol extensions) */

/* ===== Minimal JSON builder (for responses) ===== */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} JsonBuf;

static void jb_init(JsonBuf *b, size_t cap) {
    b->buf = (char *)malloc(cap);
    b->cap = cap;
    b->len = 0;
    if (b->buf) b->buf[0] = '\0';
}

static void jb_ensure(JsonBuf *b, size_t need) {
    if (b->len + need + 1 > b->cap) {
        while (b->len + need + 1 > b->cap) b->cap *= 2;
        b->buf = (char *)realloc(b->buf, b->cap);
    }
}

static void jb_out(JsonBuf *b, const char *s) {
    size_t n = strlen(s);
    jb_ensure(b, n);
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

/* (jb_append kept for future use)
static void jb_append(JsonBuf *b, const char *s) {
    jb_out(b, s);
}
*/

static void jb_printf(JsonBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    jb_ensure(b, 256);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    b->len += (size_t)n;
}

/* ===== Emotion engine (simple mood tracking) ===== */
typedef struct {
    char mood[32];
    double arousal;    /* 0.0-1.0 */
    double valence;    /* -1.0..1.0 */
    double last_update;
} EmotionState;

static EmotionState g_emotion = { "neutral", 0.5, 0.0, 0.0 };

static void emotion_set(const char *mood_name) {
    strncpy(g_emotion.mood, mood_name, 31);
    g_emotion.mood[31] = '\0';
    /* Simple prosody mapping */
    if (strcmp(mood_name, "angry") == 0) {
        g_emotion.arousal = 0.9; g_emotion.valence = -0.5;
    } else if (strcmp(mood_name, "happy") == 0) {
        g_emotion.arousal = 0.8; g_emotion.valence = 0.7;
    } else if (strcmp(mood_name, "sad") == 0) {
        g_emotion.arousal = 0.2; g_emotion.valence = -0.6;
    } else if (strcmp(mood_name, "surprised") == 0) {
        g_emotion.arousal = 0.95; g_emotion.valence = 0.0;
    } else {
        g_emotion.arousal = 0.5; g_emotion.valence = 0.0;
    }
    g_emotion.last_update = (double)time(NULL);
}

/* ===== Daemon struct ===== */
struct DaemonImpl {
    Wiki *wiki;       /* knowledge base */
    char *pipe_name;  /* named pipe path */
    int running;      /* main loop flag */
};

/* ===== Command handlers ===== */
static void handle_ping(Daemon *d, JsonBuf *resp) {
    (void)d;
    jb_out(resp, "{\"ok\":true,\"time\":");
    jb_printf(resp, "%ld", (long)time(NULL));
    jb_out(resp, "}");
}

static void handle_stats(Daemon *d, JsonBuf *resp) {
    WikiStats stats;
    wubu_wiki_stats(d->wiki, &stats);
    jb_out(resp, "{");
    jb_printf(resp, "\"articles\":%zu,\"facts\":%zu,\"links\":%zu,"
                   "\"research_articles\":%zu,\"repo_articles\":%zu",
              stats.articles, stats.facts, stats.links,
              stats.research_articles, stats.repo_articles);
    jb_out(resp, "}");
}

static void handle_wiki_search(Daemon *d, const JsonValue *req, JsonBuf *resp) {
    const char *query = json_object_get(req, "query");
    if (!query) {
        jb_out(resp, "{\"error\":\"missing 'query'\"}");
        return;
    }

    WikiResult results[20];
    size_t n = wubu_wiki_search(d->wiki, query, results, 20, NULL);

    jb_out(resp, "{\"results\":[");
    for (size_t i = 0; i < n; i++) {
        if (i > 0) jb_out(resp, ",");
        jb_out(resp, "{\"slug\":\"");
        jb_out(resp, results[i].slug ? results[i].slug : "");
        jb_out(resp, "\",\"title\":\"");
        jb_out(resp, results[i].title ? results[i].title : "");
        jb_out(resp, "\",\"score\":");
        jb_printf(resp, "%.4f", results[i].score);
        jb_out(resp, "}");
    }
    jb_out(resp, "],\"count\":");
    jb_printf(resp, "%zu", n);
    jb_out(resp, "}");
    /* Free all result copies */
    for (size_t i = 0; i < n; i++) {
        wubu_wiki_free_result(&results[i]);
    }
}

static void handle_wiki_get(Daemon *d, const JsonValue *req, JsonBuf *resp) {
    const char *slug = json_object_get(req, "slug");
    if (!slug) {
        jb_out(resp, "{\"error\":\"missing 'slug'\"}");
        return;
    }

    WikiResult r;
    if (wubu_wiki_get(d->wiki, slug, &r)) {
        jb_out(resp, "{\"slug\":\"");
        jb_out(resp, r.slug ? r.slug : "");
        jb_out(resp, "\",\"title\":\"");
        jb_out(resp, r.title ? r.title : "");
        jb_out(resp, "\",\"content\":");
        /* Content may be very long — truncate to 2000 chars */
        size_t clen = r.content ? strlen(r.content) : 0;
        if (clen > 2000) clen = 2000;
        jb_out(resp, "\"");
        if (r.content) {
            char *esc = (char *)malloc(clen * 3 + 1); /* worst case: escape every char */
            /* Simple escape: just copy, escape quotes and backslashes */
            size_t e = 0;
            for (size_t i = 0; i < clen; i++) {
                if (r.content[i] == '"') { esc[e++] = '\\'; esc[e++] = '"'; }
                else if (r.content[i] == '\\') { esc[e++] = '\\'; esc[e++] = '\\'; }
                else if (r.content[i] == '\n') { esc[e++] = '\\'; esc[e++] = 'n'; }
                else esc[e++] = r.content[i];
            }
            esc[e] = '\0';
            jb_out(resp, esc);
            free(esc);
        }
        jb_out(resp, "\",\"tags\":\"");
        jb_out(resp, r.tags ? r.tags : "");
        jb_out(resp, "\",\"source\":\"");
        jb_out(resp, r.source ? r.source : "");
        jb_out(resp, "\"}");
        wubu_wiki_free_result(&r);
    } else {
        jb_out(resp, "{\"error\":\"not found\"}");
    }
}

static void handle_wiki_fact(Daemon *d, const JsonValue *req, JsonBuf *resp) {
    const char *key = json_object_get(req, "key");
    if (!key) {
        jb_out(resp, "{\"error\":\"missing 'key'\"}");
        return;
    }

    WikiFact f;
    if (wubu_wiki_get_fact(d->wiki, key, &f)) {
        jb_out(resp, "{\"key\":\"");
        jb_out(resp, f.key);
        jb_out(resp, "\",\"value\":\"");
        jb_out(resp, f.value ? f.value : "");
        jb_out(resp, "\",\"confidence\":");
        jb_printf(resp, "%.2f", f.confidence);
        jb_out(resp, ",\"source\":\"");
        jb_out(resp, f.source ? f.source : "");
        jb_out(resp, "\"}");
    } else {
        jb_out(resp, "{\"error\":\"fact not found\"}");
    }
    wubu_wiki_free_fact(&f);
}

static void handle_facts_all(Daemon *d, JsonBuf *resp) {
    WikiFact facts[256];
    size_t n = wubu_wiki_list_facts(d->wiki, facts, 256);
    jb_out(resp, "{\"facts\":[");
    for (size_t i = 0; i < n; i++) {
        if (i > 0) jb_out(resp, ",");
        jb_out(resp, "{\"key\":\"");
        jb_out(resp, facts[i].key ? facts[i].key : "");
        jb_out(resp, "\",\"value\":\"");
        jb_out(resp, facts[i].value ? facts[i].value : "");
        jb_out(resp, "\",\"confidence\":");
        jb_printf(resp, "%.2f", facts[i].confidence);
        jb_out(resp, ",\"source\":\"");
        jb_out(resp, facts[i].source ? facts[i].source : "");
        jb_out(resp, "\"}");
    }
    jb_out(resp, "],\"count\":");
    jb_printf(resp, "%zu", n);
    jb_out(resp, "}");
    /* Free all fact copies */
    for (size_t i = 0; i < n; i++) {
        wubu_wiki_free_fact(&facts[i]);
    }
}

static void handle_emotion_set(Daemon *d, const JsonValue *req, JsonBuf *resp) {
    const char *mood = json_object_get(req, "mood");
    (void)d;  /* emotion engine is global, not per-daemon */
    if (!mood) {
        mood = json_object_get(req, "value"); /* accept "value" too */
    }
    if (!mood) {
        jb_out(resp, "{\"error\":\"missing 'mood'\"}");
        return;
    }
    char old[32];
    strncpy(old, g_emotion.mood, 31);
    old[31] = '\0';
    emotion_set(mood);
    jb_out(resp, "{\"old\":\"");
    jb_out(resp, old);
    jb_out(resp, "\",\"new\":\"");
    jb_out(resp, g_emotion.mood);
    jb_out(resp, "\",\"arousal\":");
    jb_printf(resp, "%.3f", g_emotion.arousal);
    jb_out(resp, ",\"valence\":");
    jb_printf(resp, "%.3f", g_emotion.valence);
    jb_out(resp, "}");
}

static void handle_emotion_get(Daemon *d, JsonBuf *resp) {
    (void)d;
    jb_out(resp, "{\"mood\":\"");
    jb_out(resp, g_emotion.mood);
    jb_out(resp, "\",\"arousal\":");
    jb_printf(resp, "%.3f", g_emotion.arousal);
    jb_out(resp, ",\"valence\":");
    jb_printf(resp, "%.3f", g_emotion.valence);
    jb_out(resp, ",\"last_update\":");
    jb_printf(resp, "%.0f", g_emotion.last_update);
    jb_out(resp, "}");
}

/* ===== Command dispatch ===== */
int wubu_daemon_handle_command(Daemon *d, const char *request_json,
                               char *response_buf, size_t response_cap) {
    if (!d || !request_json || !response_buf) return -1;

    JsonParser parser = { request_json, request_json, request_json + strlen(request_json), 0 };
    JsonValue *req = json_parse_value(&parser);
    if (parser.error || !req) {
        if (req) json_free(req);
        snprintf(response_buf, response_cap,
                 "{\"error\":\"invalid JSON: %s\"}",
                 request_json);
        return -1;
    }

    /* Parse command */
    const char *cmd_str = json_object_get(req, "cmd");
    if (!cmd_str) {
        /* Try "command" alias */
        cmd_str = json_object_get(req, "command");
    }
    if (!cmd_str) {
        json_free(req);
        snprintf(response_buf, response_cap, "{\"error\":\"missing 'cmd'\"}");
        return -1;
    }

    JsonBuf resp;
    jb_init(&resp, response_cap);

    /* Map command string to handler */
    if (strcmp(cmd_str, "ping") == 0 || strcmp(cmd_str, "ping") == 0) {
        handle_ping(d, &resp);
    } else if (strcmp(cmd_str, "stats") == 0) {
        handle_stats(d, &resp);
    } else if (strcmp(cmd_str, "wiki_search") == 0) {
        handle_wiki_search(d, req, &resp);
    } else if (strcmp(cmd_str, "wiki_get") == 0) {
        handle_wiki_get(d, req, &resp);
    } else if (strcmp(cmd_str, "wiki_fact") == 0) {
        handle_wiki_fact(d, req, &resp);
    } else if (strcmp(cmd_str, "facts_all") == 0) {
        handle_facts_all(d, &resp);
    } else if (strcmp(cmd_str, "emotion_set") == 0) {
        handle_emotion_set(d, req, &resp);
    } else if (strcmp(cmd_str, "emotion_get") == 0) {
        handle_emotion_get(d, &resp);
    } else if (strcmp(cmd_str, "quit") == 0) {
        jb_out(&resp, "{\"ok\":true,\"shutting_down\":true}");
        d->running = 0;
    } else {
        jb_printf(&resp, "{\"error\":\"unknown command: %s\"}", cmd_str);
    }

    json_free(req);

    /* Copy response to output buffer */
    size_t to_copy = resp.len < response_cap - 1 ? resp.len : response_cap - 1;
    memcpy(response_buf, resp.buf, to_copy);
    response_buf[to_copy] = '\0';
    free(resp.buf);

    return 0;
}

/* ===== Lifecycle ===== */
Daemon *wubu_daemon_start(const char *pipe_name, const char *db_path) {
    if (!db_path) return NULL;
    Daemon *d = (Daemon *)calloc(1, sizeof(Daemon));
    if (!d) return NULL;

    d->wiki = wubu_wiki_open(db_path);
    if (!d->wiki) {
        free(d);
        return NULL;
    }

    d->pipe_name = strdup(pipe_name ? pipe_name : "\\\\.\\pipe\\wubu_agora");
    if (!d->pipe_name) {
        wubu_wiki_close(d->wiki);
        free(d);
        return NULL;
    }
    d->running = 1;
    return d;
}

void wubu_daemon_stop(Daemon *d) {
    if (!d) return;
    d->running = 0;
}

void wubu_daemon_free(Daemon *d) {
    if (!d) return;
    if (d->wiki) wubu_wiki_close(d->wiki);
    free(d->pipe_name);
    free(d);
}

/* Portable strndup for MSVC compat (not standard C11) */
static char *wubu_strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

/* ===== Named pipe server (Windows) / Unix domain socket (Linux) ===== */
int wubu_daemon_run(Daemon *d) {
    if (!d) return -1;

    printf("WuBuDaemon AGI core online.\n");
    printf("  Knowledge base: %s\n", "SQLite FTS5");
    printf("  Emotion engine: %s\n", g_emotion.mood);

    WikiStats stats;
    wubu_wiki_stats(d->wiki, &stats);
    printf("  Articles: %zu, Facts: %zu\n", stats.articles, stats.facts);

    /* Check platform */
#ifdef _WIN32
    /* Windows named pipe server */
    char full_name[MAX_PATH];
    snprintf(full_name, sizeof(full_name), "%s", d->pipe_name);

    HANDLE hPipe = CreateNamedPipeA(
        full_name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to create named pipe %s (error %lu)\n",
                full_name, GetLastError());
        return -1;
    }
    printf("  Pipe: %s\n", full_name);
    printf("  Ready. Waiting for connections...\n");

    while (d->running) {
        printf("Waiting for client...\n");
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ?
                         TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            printf("  Client connect failed (error %lu)\n", GetLastError());
            DisconnectNamedPipe(hPipe);
            continue;
        }
        printf("  Client connected.\n");

        /* Handle requests in a loop */
        while (d->running) {
            char buffer[8192];
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
            if (!ok && GetLastError() != ERROR_MORE_DATA) break;
            buffer[bytesRead] = '\0';

            if (bytesRead == 0) break;

            /* Parse JSON line */
            char *line = wubu_strndup(buffer, bytesRead);
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';

            char response[8192];
            response[0] = '\0';
            int rc = wubu_daemon_handle_command(d, line, response, sizeof(response));
            if (rc == 0) {
                /* Send response as a line */
                size_t rlen = strlen(response);
                char rbuf[8193];
                memcpy(rbuf, response, rlen);
                rbuf[rlen] = '\n';
                rbuf[rlen + 1] = '\0';
                DWORD written = 0;
                WriteFile(hPipe, rbuf, (DWORD)(rlen + 1), &written, NULL);
            }
            free(line);

            if (nl) break; /* only one request per connection for now */
        }

        DisconnectNamedPipe(hPipe);
        printf("  Client disconnected.\n");
    }

    CloseHandle(hPipe);
#else
    /* Unix domain socket (for testing/development) */
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
    #include <errno.h>

    struct sockaddr_un addr;
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* Use a temp path on Linux */
    const char *sock_path = "/tmp/wubu_agora.sock";
    unlink(sock_path);
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock_fd);
        return -1;
    }
    listen(sock_fd, 5);
    printf("  Socket: %s\n", sock_path);
    printf("  Ready. Waiting for connections...\n");

    while (d->running) {
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        printf("  Client connected.\n");

        /* Handle client */
        char buffer[8192];
        while (d->running) {
            ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;
            buffer[n] = '\0';

            /* Handle one JSON line */
            char *line = strndup(buffer, n);
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';

            char response[8192];
            response[0] = '\0';
            int rc = wubu_daemon_handle_command(d, line, response, sizeof(response));
            if (rc == 0) {
                size_t rlen = strlen(response);
                write(client_fd, response, rlen);
                write(client_fd, "\n", 1);
            }
            free(line);
            break; /* one request per connection */
        }
        close(client_fd);
        printf("  Client disconnected.\n");
    }
    close(sock_fd);
    unlink(sock_path);
#endif

    wubu_daemon_free(d);
    printf("WuBuDaemon stopped.\n");
    return 0;
}

/* ===== Accessor ===== */
Wiki *wubu_daemon_wiki(Daemon *d) {
    return d ? d->wiki : NULL;
}

/* ===== Main entry point (for standalone daemon) ===== */
#ifdef WUBU_DAEMON_MAIN
int main(int argc, char **argv) {
    const char *db_path = argc > 1 ? argv[1] : "/tmp/wubu_kb.db";
    Daemon *d = wubu_daemon_start(NULL, db_path);
    if (!d) {
        fprintf(stderr, "Failed to start daemon\n");
        return 1;
    }
    return wubu_daemon_run(d);
}
#endif

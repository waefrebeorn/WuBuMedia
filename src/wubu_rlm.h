#ifndef WUBU_RLM_H
#define WUBU_RLM_H

/* wubu_rlm.h — Recursive Learning Memory (C11, SQLite).
 *
 * Two-tier conversation memory:
 *   1. Short-term: rolling window of recent exchanges (SQLite + in-memory cache)
 *   2. Long-term: progressively summarized exchanges (BM25-searchable via FTS5)
 *
 * Based on: "Recursively Summarizing Enables Long-Term Dialogue Memory
 *   in LLMs" (arXiv:2308.15022)
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_rlm.c -o wubu_rlm.o -lsqlite3 -Wall -Wextra
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RLMImpl RLM;

/* Summary function callback: receives buffer text, returns summary string (caller frees) */
typedef char *(*rlm_summary_fn)(const char *buffer_text, void *user_data);

/* ---------- Lifecycle ---------- */
RLM *wubu_rlm_open(const char *db_path, const char *context_slug);
void wubu_rlm_close(RLM *rlm);

/* ---------- Short-term memory ---------- */
/* Add a message to short-term memory. If buffer exceeds token threshold
 * and summary_fn is provided, summarizes the buffer.
 * Returns 1 if summarized, 0 if not. Sets *token_estimate if non-NULL. */
int wubu_rlm_add_exchange(RLM *rlm, const char *speaker, const char *text,
                          rlm_summary_fn summary_fn, void *summary_ud,
                          size_t *token_estimate);

/* Get recent exchanges (up to window_size). Fills arrays.
 * Returns count. speaker_out/text_out arrays must have room for `limit` items.
 * Strings are malloc'd — caller frees with free(). */
size_t wubu_rlm_get_context(RLM *rlm, const char *speaker_out[],
                            const char *text_out[], size_t limit);

/* ---------- Long-term recall ---------- */
/* BM25-ranked search over stored summaries. Returns count, fills out[] up to limit.
 * Each summary string is malloc'd — caller must free each. */
typedef struct {
    char  *summary;
    char  *context_slug;
    double timestamp;
    int    tokens_saved;
} RLMRecall;

size_t wubu_rlm_recall(RLM *rlm, const char *query, RLMRecall *out, size_t limit);

/* ---------- Fact storage ---------- */
int wubu_rlm_store_fact(RLM *rlm, const char *key, const char *value,
                        double confidence, const char *source);

typedef struct {
    char  *value;
    double confidence;
    char  *source;
    double updated;
} RLMFact;

int wubu_rlm_get_fact(RLM *rlm, const char *key, RLMFact *out);

/* ---------- Statistics ---------- */
typedef struct {
    size_t short_exchanges;
    size_t short_tokens;
    size_t long_summaries;
    size_t long_tokens_saved;
    size_t facts;
    const char *context;
    const char *db_path;
} RLMStats;

void wubu_rlm_stats(RLM *rlm, RLMStats *out);

/* ---------- Utility ---------- */
size_t rlm_estimate_tokens(const char *text);
void wubu_rlm_free_recall(RLMRecall *r);
void wubu_rlm_free_fact(RLMFact *f);

/* Default config values (match Python wubu_rlm.py) */
#define RLM_WINDOW_SIZE        8
#define RLM_SUMMARY_THRESHOLD  400
#define RLM_POST_SUMMARY_TOKENS 200
#define RLM_MAX_SUMMARIES      100

#ifdef __cplusplus
}
#endif

#endif /* WUBU_RLM_H */

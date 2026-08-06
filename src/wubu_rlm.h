#ifndef WUBU_RLM_H
#define WUBU_RLM_H

/* wubu_rlm.h — Recursive Learning Memory with OCEAN Personality (C11, SQLite).
 *
 * Two-tier conversation memory:
 *   1. Short-term: rolling window of recent exchanges (SQLite + in-memory cache)
 *   2. Long-term: progressively summarized exchanges (BM25-searchable via FTS5)
 *
 * Enhanced with OpenFeelz-inspired personality + emotion decay model:
 *   - OCEAN personality traits (Big Five) influence mood baselines + decay rates
 *   - Exponential decay: mood fades toward personality-influenced baseline
 *   - Rumination: persistent emotional state across interactions
 *
 * Based on:
 *   - "Recursively Summarizing Enables Long-Term Dialogue Memory in LLMs" (arXiv:2308.15022)
 *   - OpenFeelz: trianglegrrl/openfeelz (PAD + Ekman + OCEAN model)
 *   - Evolving Agents: arxiv.org/html/2404.02718v2
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_rlm.c -o wubu_rlm.o -lsqlite3 -Wall -Wextra -std=c11
 */
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RLMImpl RLM;

/* Summary function callback: receives buffer text, returns summary string (caller frees) */
typedef char *(*rlm_summary_fn)(const char *buffer_text, void *user_data);

/* ---------- OCEAN Personality (Big Five) ---------- */
/* Values range 0.0 (low) to 1.0 (high), matching OpenFeelz defaults */
typedef struct {
    double openness;       /* curiosity, creativity, openness to experience */
    double conscientiousness; /* organization, discipline, self-control */
    double extraversion;   /* sociability, energy, assertiveness */
    double agreeableness;  /* compassion, cooperation, trust */
    double neuroticism;    /* anxiety, anger, emotional instability */
} RLMPersonality;

/* ---------- Emotional State ---------- */
/* Dimensional emotion model: Valence (negative/positive) + Arousal (calm/excited) */
typedef struct {
    double valence;        /* -1.0 (sad) to +1.0 (happy) */
    double arousal;        /* 0.0 (calm) to 1.0 (excited) */
    double mood;           /* 0.0 (low) to 1.0 (high) — overall mood level */
    double energy;         /* 0.0 (low) to 1.0 (high) — energy level */
    double rumination;     /* 0.0 (none) to 1.0 (intense) — persistent emotional carryover */
    double last_update;    /* epoch timestamp of last mood update */
} RLMMood;

/* Mood classification */
typedef enum {
    Mood_Sad = 0,
    Mood_Neutral,
    Mood_Happy,
    Mood_Excited,
    Mood_VeryHappy,
    Mood_Ecstastic,
    Mood_Angry,
    Mood_Thinking,
    Mood_Tired,
    Mood_Count
} RLMMoodClass;

/* ---------- Lifecycle ---------- */
RLM *wubu_rlm_open(const char *db_path, const char *context_slug);
void   wubu_rlm_close(RLM *rlm);

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

/* ---------- Personality + Emotion ---------- */
/* Set/get OCEAN personality traits (affects mood decay rates) */
void wubu_rlm_set_personality(RLM *rlm, const RLMPersonality *p);
void wubu_rlm_get_personality(RLM *rlm, RLMPersonality *out);

/* Default personality: balanced (all 0.5) */
#define WUBU_RLM_PERSONAITY_DEFAULT 0.5

/* Apply exponential mood decay based on elapsed time + personality.
 * Returns updated mood (0.0-1.0). */
double wubu_rlm_decay_mood(RLM *rlm, const RLMMood *mood, double elapsed_seconds);

/* Update mood with a new emotional valence + arousal input.
 * Incorporates rumination: intense emotions persist longer.
 * Returns new mood value. */
double wubu_rlm_update_mood(RLM *rlm, RLMMood *mood,
                            double valence, double arousal);

/* Get current mood as a classified mood name. */
RLMMoodClass wubu_rlm_classify_mood(const RLMMood *mood);

/* Get mood name string for a classified mood. */
const char *wubu_rlm_mood_name(RLMMoodClass mc);

/* ---------- Statistics ---------- */
typedef struct {
    size_t short_exchanges;
    size_t short_tokens;
    size_t long_summaries;
    size_t long_tokens_saved;
    size_t facts;
    const char *context;
    const char *db_path;
    RLMMood current_mood;     /* current emotional state */
    RLMRecall last_recall;    /* last recall result (for context) */
} RLMStats;

void wubu_rlm_stats(RLM *rlm, RLMStats *out);

/* ---------- Utility ---------- */
size_t rlm_estimate_tokens(const char *text);
void wubu_rlm_free_recall(RLMRecall *r);
void wubu_rlm_free_fact(RLMFact *f);

/* ---------- Default config (match Python wubu_rlm.py) ---------- */
#define RLM_WINDOW_SIZE         8
#define RLM_SUMMARY_THRESHOLD   400
#define RLM_POST_SUMMARY_TOKENS  200
#define RLM_MAX_SUMMARIES       100

/* OpenFeelz-inspired decay constants (per second of elapsed time) */
#define WUBU_RLM_DECAY_JOY       0.0002   /* half-life ~57 min */
#define WUBU_RLM_DECAY_SADNESS   0.00008  /* half-life ~2.4 hours (lingers) */
#define WUBU_RLM_DECAY_ANGRY     0.00011  /* half-life ~1.6 hours */
#define WUBU_RLM_RUMINATION_THRESH 0.7    /* threshold for rumination activation */

#ifdef __cplusplus
}
#endif

#endif /* WUBU_RLM_H */

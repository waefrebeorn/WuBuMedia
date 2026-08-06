#ifndef WUBU_RECS_H
#define WUBU_RECS_H

/* wubu_recs.h — C11 recommendation engine (TikTok FYP style).
 *
 * Three-stage pipeline:
 *   1. Candidate retrieval  — two-tower style similarity
 *   2. Ranking              — weighted scoring formula (Algo 101)
 *   3. Re-ranking           — diversity injection
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_recs.c -o wubu_recs.o -lsqlite3 -lm -Wall -Wextra
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RecsImpl Recs;

/* ---- Interaction types (user events) ---- */
typedef enum {
    RECS_VIEW       = 1,  /* video was shown */
    RECS_WATCH      = 2,  /* watch (time-based weight) */
    RECS_PLAY       = 3,  /* play event (watch time) */
    RECS_COMPLETE   = 4,  /* watched to completion */
    RECS_REWATCH    = 5,  /* rewatched / looped */
    RECS_UNLIKE     = 6,
    RECS_LIKE       = 7,
    RECS_COMMENT    = 8,
    RECS_SHARE      = 9,
    RECS_SAVE       = 10,
    RECS_FOLLOW     = 11,
    RECS_SKIP       = 12,  /* swiped away quickly */
} RecsEvent;

/* ---- User interaction weights (from TikTok Algo 101) ---- */
/* Higher = stronger positive signal */
#define W_LIKE       161.2f
#define W_COMMENT    350.0f
#define W_PLAYTIME   780.6f
#define W_PLAY       999.9f
#define W_SHARE      748.6f
#define W_SAVE       748.6f
#define W_REWATCH    500.0f
#define W_COMPLETE   400.0f
#define W_FOLLOW     300.0f
#define W_VIEW       1.0f     /* baseline view */
#define W_SKIP       -300.0f  /* negative signal */
#define W_UNLIKE     -500.0f  /* negative signal */

/* ---- Candidate result ---- */
typedef struct {
    char   *slug;
    char   *title;
    char   *content;
    double  score;
    double  playtime_weight;
    double  like_weight;
    double  share_weight;
    double  save_weight;
    double  comment_weight;
    double  watch_completion;
    int     rewatches;
    size_t  feature_count;
} RecsCandidate;

/* ---- Statistics ---- */
typedef struct {
    size_t total_videos;
    size_t total_events;
    size_t total_users;
    double avg_score;
    double max_score;
} RecsStats;

/* ---- Lifecycle ---- */
Recs *wubu_recs_open(const char *db_path);
void  wubu_recs_close(Recs *r);

/* ---- Event logging ---- */
/* Log a user interaction event. Returns 1 on success. */
int wubu_recs_log_event(Recs *r, const char *user_id,
                        const char *slug, RecsEvent event,
                        double value,            /* e.g. watch time in seconds */
                        double max_value);       /* e.g. video duration */

/* ---- Content management ---- */
/* Register content for recommendation. Tags is comma-separated.
 * Returns 1 if registered/changed, 0 if unchanged. */
int wubu_recs_register_content(Recs *r, const char *slug,
                               const char *title, const char *content,
                               const char *tags, double duration_seconds);

/* ---- Candidate retrieval (Stage 1) ---- */
/* Find candidate videos similar to user's recent positive interactions.
 * Uses content-based similarity (tag overlap + TF-IDF cosine).
 * Returns count of candidates filled into out[]. */
size_t wubu_recs_candidates(Recs *r, const char *user_id,
                            RecsCandidate *out, size_t limit);

/* ---- Ranking (Stage 2) ---- */
/* Score candidates using the weighted formula.
 * Scores candidates in-place, descending. */
void wubu_recs_rank(Recs *r, RecsCandidate *candidates, size_t count);

/* ---- Re-ranking (Stage 3) ---- */
/* Inject diversity: ensure tag coverage and freshness.
 * Modifies the order in-place. */
void wubu_recs_rerank(Recs *r, RecsCandidate *candidates, size_t count);

/* ---- Full pipeline ---- */
/* Run all three stages and return top N recommendations. */
size_t wubu_recs_recommend(Recs *r, const char *user_id,
                           RecsCandidate *out, size_t limit);

/* ---- Cleanup ---- */
void wubu_recs_free_candidate(RecsCandidate *c);
void wubu_recs_free_candidates(RecsCandidate *c, size_t count);

/* ---- Stats ---- */
void wubu_recs_stats(Recs *r, RecsStats *out);

/* ---- Config ---- */
typedef struct {
    double diversity_factor;   /* 0.0 = pure ranking, 1.0 = max diversity */
    double freshness_days;    /* days: recent content gets boost */
    double min_score;         /* minimum score to include */
} RecsConfig;

extern RecsConfig recs_config;

#ifdef __cplusplus
}
#endif

#endif /* WUBU_RECS_H */

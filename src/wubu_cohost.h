#ifndef WUBU_COHOST_H
#define WUBU_COHOST_H

/* wubu_cohost.h — C11 cohost persona integrating all WuBuDesk modules.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_cohost.c -o wubu_cohost.o -lsqlite3 -lm -Wall -Wextra
 */
#include <stddef.h>
#include "wubu_wiki.h"
#include "wubu_emotion.h"
#include "wubu_recs.h"
#include "wubu_rlm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CohostImpl Cohost;

/* ---------- Statistics ---------- */
typedef struct {
    double     mood;
    double     energy;
    WikiStats  wiki_stats;
    RLMStats   rlm_stats;
    RecsStats  recs_stats;
} CohostStats;

/* ---------- Lifecycle ---------- */
Cohost *wubu_cohost_create(const char *db_path, const char *persona_name);
void   wubu_cohost_destroy(Cohost *c);

/* ---------- Persona ---------- */
const char *wubu_cohost_prompt(Cohost *c);
const char *wubu_cohost_name(Cohost *c);

/* ---------- Emotion / Mood ---------- */
void wubu_cohost_update_emotion(Cohost *c, const char *text);

/* ---------- Memory (RLM) ---------- */
int wubu_cohost_remember(Cohost *c, const char *speaker, const char *text);
char *wubu_cohost_recall(Cohost *c, const char *query);
int wubu_cohost_store_fact(Cohost *c, const char *key, const char *value,
                            double confidence);

/* ---------- Knowledge lookup ---------- */
int wubu_cohost_lookup(Cohost *c, const char *key, WikiFact *out);

/* ---------- Internal accessors (for testing/integration) ---------- */
Recs *wubu_cohost_recs(Cohost *c);
RLM  *wubu_cohost_rlm(Cohost *c);

Wiki *wubu_cohost_wiki(Cohost *c);
size_t wubu_cohost_recommend(Cohost *c, const char *user_id,
                              RecsCandidate *out, size_t limit);

/* ---------- Statistics ---------- */
void wubu_cohost_stats(Cohost *c, CohostStats *out);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_COHOST_H */

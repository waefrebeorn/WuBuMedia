/* wubu_cohost.c — C11 cohost persona integrating all WuBuDesk modules.
 *
 * Ports the Python wubu_cohost.py persona runtime to native C11.
 * Uses wubu_wiki for knowledge, wubu_rlm for memory, wubu_recs for
 * content selection, and wubu_emotion for prosodic analysis.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_cohost.c -o wubu_cohost.o -lsqlite3 -lm -Wall -Wextra
 */
#include "wubu_cohost.h"
#include "wubu_wiki.h"
#include "wubu_emotion.h"
#include "wubu_recs.h"
#include "wubu_rlm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---------- Opaque struct ---------- */
struct CohostImpl {
    Wiki       *wiki;
    Emotion    *emotion;
    Recs       *recs;
    RLM        *rlm;
    char       *persona_name;
    char       *persona_prompt;
    double      mood;      /* 0.0 = neutral, 1.0 = ecstatic */
    double      energy;    /* 0.0 = idle, 1.0 = active */
};

/* ---------- Persona ---------- */
#define DEFAULT_PROMPT \
    "You are WuBu, the WuBuMedia cohost. You are a cowboy engineer AGI " \
    "running on a streaming PC. You help WaefreBeorn build AGI while " \
    "commentating on the development process. You have access to a knowledge " \
    "base, emotion engine, and content recommendation system. " \
    "You speak with a mix of technical expertise and casual personality."

Cohost *wubu_cohost_create(const char *db_path, const char *persona_name) {
    Cohost *c = (Cohost *)calloc(1, sizeof(Cohost));
    if (!c) return NULL;

    /* Wiki KB — optional */
    c->wiki = wubu_wiki_open(db_path);

    /* Emotion engine — optional */
    c->emotion = wubu_emotion_open();

    /* Recs engine — use a separate DB for event logs */
    c->recs = wubu_recs_open("/tmp/wubu_recs.db");

    /* RLM memory — session-scoped */
    c->rlm = wubu_rlm_open("/tmp/wubu_rlm.db", persona_name ? persona_name : "cohort");

    c->persona_name = persona_name ? strdup(persona_name) : strdup("WuBu");
    c->persona_prompt = strdup(DEFAULT_PROMPT);
    c->mood = 0.5;    /* neutral */
    c->energy = 0.3;  /* idle */

    return c;
}

void wubu_cohost_destroy(Cohost *c) {
    if (!c) return;
    if (c->wiki) wubu_wiki_close(c->wiki);
    if (c->emotion) wubu_emotion_close(c->emotion);
    if (c->recs) wubu_recs_close(c->recs);
    if (c->rlm) wubu_rlm_close(c->rlm);
    free(c->persona_name);
    free(c->persona_prompt);
    free(c);
}

/* ---------- Persona ---------- */
const char *wubu_cohost_prompt(Cohost *c) {
    return c ? c->persona_prompt : DEFAULT_PROMPT;
}

const char *wubu_cohost_name(Cohost *c) {
    return c ? c->persona_name : "WuBu";
}

/* ---------- Emotion / Mood ---------- */
/* Update cohost mood based on processed text.
 * Uses lightweight text sentiment analysis for text-only input.
 * For real-time voice analysis, use wubu_emotion_frame() on PCM samples. */
void wubu_cohost_update_emotion(Cohost *c, const char *text) {
    if (!c || !text) return;

    /* Count emotional words in text */
    int happy = 0, sad = 0, excited = 0, angry = 0;
    const char *p = text;
    while (p && *p) {
        /* Find next word boundary */
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (*p == '\0') break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\0') p++;
        size_t len = p - start;

        char lower[64];
        for (size_t i = 0; i < len && i < sizeof(lower) - 1; i++)
            lower[i] = (char)tolower((unsigned char)start[i]);
        lower[len < sizeof(lower) ? len : sizeof(lower) - 1] = '\0';

        if (strstr(lower, "excit") || strstr(lower, "amaz") || strstr(lower, "love")) excited++;
        if (strstr(lower, "happy") || strstr(lower, "great") || strstr(lower, "love")) happy++;
        if (strstr(lower, "sad") || strstr(lower, "bad") || strstr(lower, "hard")) sad++;
        if (strstr(lower, "angry") || strstr(lower, "frustrat")) angry++;
    }

    /* Map word counts to mood (0.0-1.0) */
    int total = happy + excited + sad + angry;
    if (total == 0) return;

    double positivity = (double)(happy + excited) / (double)total;
    double energy_level = (double)(excited + happy) / (double)total;

    c->mood += (positivity - c->mood) * 0.3;
    c->energy += (energy_level - c->energy) * 0.3;

    if (c->mood < 0.0) c->mood = 0.0;
    if (c->mood > 1.0) c->mood = 1.0;
    if (c->energy < 0.0) c->energy = 0.0;
    if (c->energy > 1.0) c->energy = 1.0;
}

/* ---------- Memory (RLM) ---------- */
int wubu_cohost_remember(Cohost *c, const char *speaker, const char *text) {
    if (!c || !speaker || !text) return 0;
    if (!c->rlm) return 0;
    size_t tokens;
    wubu_rlm_add_exchange(c->rlm, speaker, text, NULL, NULL, &tokens);
    (void)tokens;
    return 1;
}

char *wubu_cohost_recall(Cohost *c, const char *query) {
    if (!c || !query || !c->rlm) return NULL;
    RLMRecall results[5];
    size_t n = wubu_rlm_recall(c->rlm, query, results, 5);
    if (n == 0) return NULL;
    char *result = strdup(results[0].summary ? results[0].summary : "");
    for (size_t i = 0; i < n; i++) {
        wubu_rlm_free_recall(&results[i]);
    }
    return result;
}

/* Store a structured fact. Returns 1 on success.
 * Stores in both wiki (for search) and RLM (for recall). */
int wubu_cohost_store_fact(Cohost *c, const char *key, const char *value,
                            double confidence) {
    if (!c || !key || !value) return 0;
    int ok = 1;
    if (c->rlm) ok &= wubu_rlm_store_fact(c->rlm, key, value, confidence, "cohort");
    if (c->wiki) ok &= wubu_wiki_put_fact(c->wiki, key, value, confidence, "cohort");
    return ok;
}

/* ---------- Knowledge lookup ---------- */
int wubu_cohost_lookup(Cohost *c, const char *key, WikiFact *out) {
    if (!c || !c->wiki) return 0;
    return wubu_wiki_get_fact(c->wiki, key, out);
}

/* ---------- Content recommendation ---------- */
size_t wubu_cohost_recommend(Cohost *c, const char *user_id,
                              RecsCandidate *out, size_t limit) {
    if (!c || !c->recs) return 0;
    return wubu_recs_recommend(c->recs, user_id, out, limit);
}

/* ---------- Recs accessors (for test/external setup) ---------- */
Recs *wubu_cohost_recs(Cohost *c) {
    return c ? c->recs : NULL;
}

RLM *wubu_cohost_rlm(Cohost *c) {
    return c ? c->rlm : NULL;
}

Wiki *wubu_cohost_wiki(Cohost *c) {
    return c ? c->wiki : NULL;
}

/* ---------- Statistics ---------- */
void wubu_cohost_stats(Cohost *c, CohostStats *out) {
    if (!out) return;
    memset(out, 0, sizeof(CohostStats));
    if (!c) return;

    out->mood = c->mood;
    out->energy = c->energy;

    if (c->wiki) wubu_wiki_stats(c->wiki, &out->wiki_stats);
    if (c->rlm) wubu_rlm_stats(c->rlm, &out->rlm_stats);
    if (c->recs) wubu_recs_stats(c->recs, &out->recs_stats);
}

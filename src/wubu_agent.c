/* wubu_agent.c — C11 agent entry point bridging slermes agent with WuBuMedia.
 *
 * Integrates the slermes C11 agent loop with WuBuDesk cohost infrastructure.
 * The agent uses the cohost's knowledge base, memory, emotion, and
 * recommendation engine to generate contextually aware responses.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_agent.c -o wubu_agent.o -lsqlite3 -lm -Wall -Wextra -std=c11
 */
#include "wubu_agent.h"
#include "wubu_cohost.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------- Agent state ---------- */
struct WubuAgent {
    Cohost   *cohost;
    char     *config_path;
    size_t    exchange_count;
    size_t    total_tokens;
    double    mood_sum;
    double    energy_sum;
};

/* ---------- Lifecycle ---------- */
WubuAgent *wubu_agent_create(const char *config_path, Cohost *cohost) {
    if (!cohost) return NULL;

    WubuAgent *a = (WubuAgent *)calloc(1, sizeof(WubuAgent));
    if (!a) return NULL;

    a->cohost = cohost;
    a->config_path = config_path ? strdup(config_path) : NULL;
    a->exchange_count = 0;
    a->total_tokens = 0;
    a->mood_sum = 0.5;
    a->energy_sum = 0.3;

    return a;
}

void wubu_agent_destroy(WubuAgent *a) {
    if (!a) return;
    free(a->config_path);
    /* Note: does NOT free cohost — it may be shared */
    free(a);
}

/* ---------- Context building ---------- */
/* Build a context string that includes persona prompt, mood, relevant
 * knowledge, and conversation history — all fed to the LLM. */
char *wubu_agent_build_context(WubuAgent *a, const char *query) {
    if (!a || !query) return NULL;

    CohostStats stats;
    wubu_cohost_stats(a->cohost, &stats);

    /* Get relevant knowledge from wiki */
    WikiFact wf;
    int has_fact = wubu_cohost_lookup(a->cohost, query, &wf);

    /* Get relevant conversation history */
    char *recall = wubu_cohost_recall(a->cohost, query);

    /* Build context JSON-like string */
    size_t cap = 4096;
    char *ctx = malloc(cap);
    if (!ctx) {
        if (has_fact) wubu_wiki_free_fact(&wf);
        free(recall);
        return NULL;
    }

    int n = snprintf(ctx, cap,
        "[[SYSTEM PROMPT]]\n%s\n\n"
        "[[MOOD]]\nCurrent mood: %.2f, Energy: %.2f\n\n"
        "[[KNOWLEDGE]]\n",
        wubu_cohost_prompt(a->cohost),
        stats.mood, stats.energy);

    if (has_fact) {
        n += snprintf(ctx + n, cap - n, "Fact [%s]: %s (confidence: %.2f)\n",
            wf.key ? wf.key : "?", wf.value ? wf.value : "?", wf.confidence);
        wubu_wiki_free_fact(&wf);
    }

    if (recall) {
        n += snprintf(ctx + n, cap - n, "\n[[CONV_HISTORY]]\n%s\n", recall);
        free(recall);
    }

    n += snprintf(ctx + n, cap - n, "\n[[CURRENT QUESTION]]\n%s\n", query);

    return ctx;
}

/* ---------- Chat (stub — integrates with slermes agent loop) ---------- */
/* Full integration with slermes requires linking against slermes object
 * files and running the agent loop. This stub demonstrates the pattern:
 * build context → call agent → remember + update emotion. */
char *wubu_agent_chat(WubuAgent *a, const char *user_message) {
    if (!a || !user_message) return NULL;

    /* Remember the exchange */
    wubu_cohost_remember(a->cohost, "user", user_message);

    /* Update mood from message sentiment */
    wubu_cohost_update_emotion(a->cohost, user_message);

    /* Build context for LLM (would be passed to slerms agent_run_conversation) */
    char *context = wubu_agent_build_context(a, user_message);

    /* STUB: In full integration, this would call:
     *   agent_state_t state;
     *   init_agent(&state);
     *   agent_configure_from_config(&state, cfg);
     *   char *response = agent_run_conversation(&state, user_message, context);
     *   agent_free(&state);
     *
     * The slermes agent provides: tool calling, session persistence,
     * checkpoint/restore, context compression, streaming, and interrupt
     * handling — all in C11. The cohost modules provide domain-specific
     * tools (wiki lookup, memory, emotion, recs) that the agent can call.
     */

    /* Simulate a response */
    CohostStats stats;
    wubu_cohost_stats(a->cohost, &stats);

    char *response = malloc(1024);
    if (!response) {
        free(context);
        return NULL;
    }

    snprintf(response, 1024,
        "[WuBu] Got your message! Mood: %.1f/1.0, Energy: %.1f/1.0, "
        "Wiki articles: %zu, Facts: %zu, Recs: %.2f avg score. "
        "Context built (%zu chars). Ready for slermes integration.",
        stats.mood, stats.energy,
        stats.wiki_stats.articles,
        stats.rlm_stats.facts,
        stats.recs_stats.avg_score,
        context ? strlen(context) : 0);

    a->exchange_count++;
    a->total_tokens += (size_t)strlen(user_message) / 4;
    a->mood_sum = (a->mood_sum * (a->exchange_count - 1) + stats.mood) / a->exchange_count;
    a->energy_sum = (a->energy_sum * (a->exchange_count - 1) + stats.energy) / a->exchange_count;

    free(context);
    return response;
}

/* ---------- Stats ---------- */
void wubu_agent_stats(WubuAgent *a, WubuAgentStats *out) {
    if (!out) return;
    memset(out, 0, sizeof(WubuAgentStats));
    if (!a) return;
    out->total_exchanges = a->exchange_count;
    out->total_tokens = a->total_tokens;
    out->avg_mood = a->exchange_count > 0 ? a->mood_sum / a->exchange_count : 0.5;
    out->avg_energy = a->exchange_count > 0 ? a->energy_sum / a->exchange_count : 0.3;
}

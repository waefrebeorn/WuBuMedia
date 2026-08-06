#ifndef WUBU_AGENT_H
#define WUBU_AGENT_H

/* wubu_agent.h — C11 agent entry point bridging slermes agent with WuBuMedia.
 *
 * This module integrates the slermes C11 agent loop with the WuBuDesk
 * cohost infrastructure (wiki, RLM, recs, emotion, face, OBS, Twitch).
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_agent.c -o wubu_agent.o -Wall -Wextra -std=c11
 */
#include <stddef.h>
#include "wubu_cohost.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Agent lifecycle */
typedef struct WubuAgent WubuAgent;

/* Create a new agent instance.
 * config_path: path to agent config JSON
 * cohost: already-initialized cohost
 * Returns NULL on failure. */
WubuAgent *wubu_agent_create(const char *config_path, Cohost *cohost);

/* Destroy an agent instance. Safe with NULL. */
void wubu_agent_destroy(WubuAgent *a);

/* Process a single user message.
 * Returns malloc'd response string (caller must free). */
char *wubu_agent_chat(WubuAgent *a, const char *user_message);

/* Get agent statistics */
typedef struct {
    size_t total_exchanges;
    size_t total_tokens;
    double avg_mood;
    double avg_energy;
} WubuAgentStats;

void wubu_agent_stats(WubuAgent *a, WubuAgentStats *out);

/* Get persona-aware context for LLM (includes mood, knowledge, recommendations).
 * Returns malloc'd JSON string (caller must free). */
char *wubu_agent_build_context(WubuAgent *a, const char *query);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_AGENT_H */

#ifndef WUBU_GATEWAY_H
#define WUBU_GATEWAY_H

/* wubu_gateway.h — C11 HTTP API gateway for the WuBuDesk AGI cohost.
 *
 * Provides a minimal HTTP server with JSON API endpoints that bridge
 * the cohost persona with external tools (OBS, Telegram, browser extensions).
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_gateway.c -o wubu_gateway.o -Wall -Wextra -std=c11
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — Cohost is opaque (defined in wubu_cohost.h) */
typedef struct CohostImpl Cohost;

typedef struct GatewayImpl Gateway;

/* Create a new gateway.
 * token: Auth token for Bearer auth (NULL = dev mode, no auth required).
 * port:  HTTP listen port (default: 18763).
 * cohost: Already-initialized cohost instance (required). */
Gateway *wubu_gateway_create(const char *token, int port, Cohost *cohost);

/* Destroy the gateway. Safe with NULL. */
void wubu_gateway_destroy(Gateway *gw);

/* Start the HTTP server (blocking accept loop).
 * Returns 0 on success, -1 on bind failure. */
int wubu_gateway_start(Gateway *gw);

/* Stop the server. */
void wubu_gateway_stop(Gateway *gw);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_GATEWAY_H */

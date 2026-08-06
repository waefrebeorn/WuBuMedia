#ifndef WUBU_DAEMON_H
#define WUBU_DAEMON_H
/* wubu_daemon.h — C11 daemon for the cohost AGI core.
 *
 * Opaque-struct API: manages the knowledge base (wubu_wiki),
 * emotion engine, and IPC via Windows named pipes.
 *
 * License: SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: cc -c wubu_daemon.c -o wubu_daemon.o -lsqlite3 -lws2_32 -lkernel32
 */
#include <stddef.h>
#include <stdint.h>
#include "wubu_wiki.h"  /* for Wiki type used in wubu_daemon_wiki */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Opaque daemon handle ---------- */
typedef struct DaemonImpl Daemon;

/* ---------- Command protocol (sent over named pipes) ---------- */
typedef enum {
    CMD_PING = 1,        /* no payload -> { "ok": true } */
    CMD_WIKI_SEARCH,     /* query string -> results array */
    CMD_WIKI_GET,        /* slug -> article */
    CMD_WIKI_UPSERT,     /* article fields -> { "changed": N } */
    CMD_WIKI_FACT,       /* key -> { "value": "...", "confidence": N } */
    CMD_FACTS_ALL,       /* no payload -> all facts array */
    CMD_STATS,           /* no payload -> { articles, facts, ... } */
    CMD_EMOTION_SET,     /* mood name -> { "old": "...", "new": "..." } */
    CMD_EMOTION_GET,     /* no payload -> current mood + prosody */
    CMD_QUIT,            /* no payload -> shutdown */
} DaemonCmd;

/* ---------- Lifecycle ---------- */
/* Start the daemon on the given pipe name. Returns NULL on failure. */
Daemon *wubu_daemon_start(const char *pipe_name, const char *db_path);

/* Run the main loop (blocking). Returns exit code. */
int wubu_daemon_run(Daemon *d);

/* Shut down gracefully. Safe to call from another thread. */
void   wubu_daemon_stop(Daemon *d);

/* Free a daemon that was started but not run (or after stop). */
void wubu_daemon_free(Daemon *d);

/* ---------- Direct API (bypass pipes for in-process use) ---------- */
int wubu_daemon_handle_command(Daemon *d, const char *request_json,
                               char *response_buf, size_t response_cap);

/* Get the internal wiki handle for direct access (testing). */
Wiki *wubu_daemon_wiki(Daemon *d);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_DAEMON_H */

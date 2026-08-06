#ifndef WUBU_WSS_H
#define WUBU_WSS_H

/* wubu_wss.h — Minimal C11 WebSocket server for face state push.
 * License: WaefreBeorn-UMV3
 */
#include <stddef.h>
#include "wubu_face.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WSSImpl WSS;

/* Create a WebSocket server on the given port, pushing face state updates. */
WSS *wubu_wss_create(int port, Face *face);
void wubu_wss_destroy(WSS *ws);

/* Start the server (non-blocking on accept). Returns 0 on success. */
int wubu_wss_start(WSS *ws);
void wubu_wss_stop(WSS *ws);

/* Poll for new connections and push face state. Returns:
 *   0 = no new client, no disconnect
 *   1 = new client connected
 *  -1 = client disconnected */
int wubu_wss_poll(WSS *ws);

/* Push current face state to all connected clients. */
int wubu_wss_push_face(WSS *ws);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_WSS_H */

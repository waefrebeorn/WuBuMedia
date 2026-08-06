#ifndef WUBU_SELF_H
#define WUBU_SELF_H

/* wubu_self.h — C11 self-improvement scheduler for the cohost AGI.
 * License: WaefreBeorn-UMV3
 */
#include <stddef.h>
#include <time.h>
#include "wubu_wiki.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SelfImpl Self;

/* A single check result */
typedef struct {
    char *name;
    char *message;
    int   pass;
} SelfCheckResult;

/* Full report from one self-check cycle */
typedef struct {
    double           timestamp;
    size_t           check_count;
    SelfCheckResult *checks;
    char             message_buf[6][512];  /* scratch buffers for check messages */
} SelfReport;

/* Lifecycle */
Self *wubu_self_create(Wiki *wiki, const char *log_path, int interval_seconds);
void wubu_self_destroy(Self *s);

/* Run one self-check cycle. Returns 1 if all checks passed. */
int wubu_self_check(Self *s, SelfReport *out);

/* Free a report. Must be called after wubu_self_check. */
void wubu_self_free_report(SelfReport *r);

/* Blocking scheduler loop. Returns 0 when stopped. */
int wubu_self_run_scheduler(Self *s);

/* Signal the scheduler to stop. */
void wubu_self_stop(Self *s);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_SELF_H */

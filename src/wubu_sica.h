/* wubu_sica.h — Self-Improving Coding Agent loop (C11).
 *
 * Implements the SICA 5-stage loop with container isolation + git rollback:
 *   1. Capture traces — run agent on current task, collect logs
 *   2. Analyze — identify improvements (via RLM fact analysis)
 *   3. Edit — apply code changes (via Claude Code / shell)
 *   4. Validate — run tests (build_c11.sh test)
 *   5. Commit or Rollback — git commit OR git checkout (rollback)
 *
 * Runs on a 15-minute timer by default.
 *
 * Research basis:
 *   - arxiv.org/html/2504.15228v1 (SICA paper)
 *   - addyosmani.com/blog/self-improving-agents/ (Ralph Wiggum technique)
 *   - anthropic.com/institute/recursive-self-improvement
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_sica.c -o wubu_sica.o -lsqlite3 -Wall -Wextra -std=c11
 */
#ifndef WUBU_SICA_H
#define WUBU_SICA_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle */
typedef struct SicImpl Sica;

/* ---------- Configuration ---------- */
typedef struct {
    int  interval_seconds;      /* Default: 900 (15 min) */
    int  max_iterations;        /* Default: 100 */
    int  auto_commit;           /* Commit if tests pass (default: 1) */
    int  auto_rollback;         /* Rollback on test failure (default: 1) */
    char knowledge_dir[256];    /* Path to knowledge/ for research integration */
    char repo_dir[256];         /* Path to repo root (for git operations) */
} SicaConfig;

/* ---------- Cycle report ---------- */
typedef struct {
    double  timestamp;
    int     cycle_number;
    int     success;            /* 1 = committed, 0 = rolled back */
    int     tests_before;
    int     tests_after;
    char    improvement[512];   /* Description of what was improved */
    char    error[512];         /* Error message if failed */
    double  elapsed_seconds;
} SicaCycleReport;

/* Lifecycle */
Sica *wubu_sica_create(const SicaConfig *cfg);
void   wubu_sica_destroy(Sica *s);

/* Run one SICA cycle (capture + analyze + edit + validate + commit/rollback). */
int wubu_sica_run_cycle(Sica *s, const char *task_description, SicaCycleReport *out);

/* Run the 15-minute scheduler loop (blocking). */
int wubu_sica_run_scheduler(Sica *s, const char *task_description);

/* Stop the scheduler. */
void wubu_sica_stop(Sica *s);

/* Get current cycle count. */
int wubu_sica_cycle_count(Sica *s);

/* ---------- Container isolation helpers ---------- */
/* Run a command in an isolated context (fresh process, no context carryover).
 * Returns exit code. Output written to stdout/stderr. */
int wubu_sica_run_isolated(const char *cmd, const char *cwd, char *out, size_t cap);

/* Git helpers for commit + rollback */
int wubu_sica_git_commit(const char *repo_dir, const char *message);
int wubu_sica_git_rollback(const char *repo_dir);
int wubu_sica_git_has_changes(const char *repo_dir);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_SICA_H */

/* wubu_self.c — C11 self-improvement scheduler for the cohost AGI.
 *
 * Replaces the Python wubu_self.py with native C11.
 * Runs periodic self-checks: wiki health, module compilation,
 * face state liveness, system health, knowledge ingestions.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_self.c -o wubu_self.o -Wall -Wextra -std=c11
 */
#include "wubu_self.h"
#include "wubu_wiki.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

/* ---------- Opaque struct ---------- */
struct SelfImpl {
    Wiki       *wiki;
    char       *log_path;
    time_t      last_check;
    int         check_interval;
    int         running;
};

/* ---------- Logging ---------- */
static void self_log(Self *s, const char *level, const char *msg) {
    if (!s || !msg) return;

    time_t now = time(NULL);
    char tbuf[64];
    FILE *f = fopen(s->log_path, "a");

#ifdef _WIN32
    struct tm tm_info;
    localtime_s(&tm_info, &now);
#else
    struct tm tm_info;
    localtime_r(&now, &tm_info);
#endif
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%S", &tm_info);

    if (f) {
        fprintf(f, "[%s] [%s] %s\n", tbuf, level, msg);
        fclose(f);
    }
    printf("[%s] [%s] %s\n", tbuf, level, msg);
}

/* ---------- Check 1: Wiki health ---------- */
static int self_check_wiki(Self *s, char *out, size_t cap) {
    if (!s->wiki) {
        snprintf(out, cap, "wiki: not available");
        return 0;
    }
    WikiStats stats;
    wubu_wiki_stats(s->wiki, &stats);
    int ok = (stats.articles > 0);
    snprintf(out, cap, "wiki: %zu articles, %zu facts (%s)",
             stats.articles, stats.facts, ok ? "OK" : "WARN");
    return ok;
}

/* ---------- Check 2: Module compilation (C11) ---------- */
static int self_check_modules(char *out, size_t cap) {
    /* Check that our C11 modules compile — invoke the build script */
#ifdef _WIN32
    int rc = system("sh build_c11.sh 2>nul");
#else
    int rc = system("sh build_c11.sh 2>/dev/null");
#endif
    int ok = (rc == 0);
    snprintf(out, cap, "modules: %s (rc=%d)", ok ? "compile clean" : "ERRORS", rc);
    return ok;
}

/* ---------- Check 3: Face state liveness ---------- */
static int self_check_face(char *out, size_t cap) {
    const char *face_dir = getenv("WUBU_FACE_DIR");
    char path[512];
    if (face_dir)
        snprintf(path, sizeof(path), "%s/face_state.json", face_dir);
    else
        snprintf(path, sizeof(path), "face/face_state.json");

#ifdef _WIN32
    struct _stat st;
    if (_stat(path, &st) != 0) {
        snprintf(out, cap, "face: face_state.json missing");
        return 0;
    }
    time_t age = time(NULL) - st.st_mtime;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(out, cap, "face: face_state.json missing");
        return 0;
    }
    time_t age = time(NULL) - st.st_mtime;
#endif
    int stale = age > 30;
    snprintf(out, cap, "face: age=%lds (%s)", (long)age, stale ? "STALE" : "alive");
    return !stale;
}

/* ---------- Check 4: System health ---------- */
static int self_check_system(char *out, size_t cap) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        size_t mem_kb = pmc.WorkingSetSize / 1024;
        size_t mem_mb = mem_kb / 1024;
        snprintf(out, cap, "system: mem=%zuMB", mem_mb);
        return (mem_mb < 4096);
    }
    snprintf(out, cap, "system: unable to query (Windows)");
    return 1;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        size_t mem_kb = usage.ru_maxrss;
        size_t mem_mb = mem_kb / 1024;
        snprintf(out, cap, "system: mem=%zuMB", mem_mb);
        return (mem_mb < 4096);
    }
    snprintf(out, cap, "system: unable to query");
    return 1;
#endif
}

/* ---------- Run one self-check cycle ---------- */
int wubu_self_check(Self *s, SelfReport *out) {
    if (!s || !out) return 0;

    memset(out, 0, sizeof(SelfReport));
    out->timestamp = (double)time(NULL);
    out->check_count = 6;
    out->checks = (SelfCheckResult *)calloc(out->check_count, sizeof(SelfCheckResult));
    if (!out->checks) return 0;

    self_log(s, "INFO", "Starting self-improvement cycle");

    out->checks[0].name = strdup("wiki");
    out->checks[0].pass = self_check_wiki(s, out->message_buf[0], sizeof(out->message_buf[0]));
    out->checks[0].message = strdup(out->message_buf[0]);

    out->checks[1].name = strdup("modules");
    out->checks[1].pass = self_check_modules(out->message_buf[1], sizeof(out->message_buf[1]));
    out->checks[1].message = strdup(out->message_buf[1]);

    out->checks[2].name = strdup("face");
    out->checks[2].pass = self_check_face(out->message_buf[2], sizeof(out->message_buf[2]));
    out->checks[2].message = strdup(out->message_buf[2]);

    out->checks[3].name = strdup("system");
    out->checks[3].pass = self_check_system(out->message_buf[3], sizeof(out->message_buf[3]));
    out->checks[3].message = strdup(out->message_buf[3]);

    /* Check 5: Memory health (RLM facts) */
    out->checks[4].name = strdup("rlm");
    /* Check 6: Recommendation engine */
    out->checks[5].name = strdup("recs");

    for (size_t i = 0; i < out->check_count; i++) {
        self_log(s, out->checks[i].pass ? "INFO" : "WARN", out->checks[i].message);
    }

    int all_pass = 1;
    for (size_t i = 0; i < out->check_count; i++) {
        if (!out->checks[i].pass) all_pass = 0;
    }
    self_log(s, "INFO", all_pass ? "All checks passed" : "Some checks failed");

    s->last_check = time(NULL);
    return all_pass;
}

void wubu_self_free_report(SelfReport *r) {
    if (!r) return;
    for (size_t i = 0; i < r->check_count; i++) {
        free(r->checks[i].name);
        free(r->checks[i].message);
    }
    free(r->checks);
    memset(r, 0, sizeof(SelfReport));
}

/* ---------- Lifecycle ---------- */
Self *wubu_self_create(Wiki *wiki, const char *log_path, int interval_seconds) {
    Self *s = (Self *)calloc(1, sizeof(Self));
    if (!s) return NULL;
    s->wiki = wiki;
    s->log_path = log_path ? strdup(log_path) : strdup("knowledge/self_improvement.log");
    s->check_interval = interval_seconds;
    s->last_check = 0;
    s->running = 0;
    return s;
}

void wubu_self_destroy(Self *s) {
    if (!s) return;
    free(s->log_path);
    free(s);
}

/* ---------- Scheduler (blocking loop) ---------- */
int wubu_self_run_scheduler(Self *s) {
    if (!s) return -1;
    s->running = 1;
    self_log(s, "INFO", "Self-improvement scheduler started");

    while (s->running) {
        SelfReport report;
        wubu_self_check(s, &report);
        wubu_self_free_report(&report);

#ifdef _WIN32
        Sleep(s->check_interval * 1000);
#else
        sleep((unsigned int)s->check_interval);
#endif
    }
    return 0;
}

void wubu_self_stop(Self *s) {
    if (s) s->running = 0;
}

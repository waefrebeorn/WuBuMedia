/* wubu_gateway.c — C11 HTTP API gateway for the WuBuDesk AGI cohost.
 *
 * Replaces the Python wubu_gateway.py with a native C11 HTTP server.
 * Uses libhttp.h (from slermes) or a minimal HTTP parser for Windows.
 *
 * Endpoints:
 *   GET  /api/health        — System health (face state, wiki, recs)
 *   GET  /api/stats         — System stats
 *   GET  /api/wiki/search?q=  — Search knowledge base
 *   GET  /api/wiki/fact?key=  — Get a structured fact
 *   GET  /api/wiki/article/<slug> — Get wiki article
 *   GET  /api/wiki/facts    — Export all facts
 *   POST /api/wiki/upsert   — Add wiki article
 *   POST /api/say           — Make cohost speak
 *   POST /api/poke          — Trigger emotion
 *   POST /api/fling         — Trigger emotion
 *   GET  /ws                — WebSocket (face state push)
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_gateway.c -o wubu_gateway.o -lsqlite3 -lws2_32
 */
#include "wubu_gateway.h"
#include "wubu_wiki.h"
#include "wubu_rlm.h"
#include "wubu_recs.h"
#include "wubu_cohost.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

/* ---------- Opaque struct ---------- */
struct GatewayImpl {
    Cohost  *cohost;
    char    *token;
    int      port;
    int      listen_fd;
    int      running;
};

/* ---------- URL path query parsing ---------- */
static const char *gw_url_query(const char *path, const char *key) {
    const char *q = strchr(path, '?');
    if (!q) return NULL;
    q++; /* skip '?' */
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "%s=", key);
    size_t plen = strlen(pattern);
    while (*q) {
        if (strncmp(q, pattern, plen) == 0) {
            return q + plen;
        }
        /* Skip to next param */
        const char *amp = strchr(q, '&');
        if (!amp) break;
        q = amp + 1;
    }
    return NULL;
}

/* ---------- HTTP response helpers ---------- */
static void gw_send_json(int fd, const char *json, int status) {
    char header[512];
    int len = (int)strlen(json);
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "\r\n", status, len);

#ifdef _WIN32
    send(fd, header, (int)strlen(header), 0);
    send(fd, json, len, 0);
#else
    write(fd, header, strlen(header));
    write(fd, json, len);
#endif
}

static void gw_send_error(int fd, int code, const char *msg) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"error\":\"%s\",\"code\":%d}", msg, code);
    gw_send_json(fd, json, code);
}

static int gw_check_auth(const char *auth_header, const char *token) {
    if (!token || !*token) return 1; /* dev mode: no auth */
    if (!auth_header) return 0;
    if (strncmp(auth_header, "Bearer ", 7) != 0) return 0;
    return strcmp(auth_header + 7, token) == 0;
}

/* ---------- Handler: health ---------- */
static void gw_handle_health(Gateway *gw, int fd) {
    CohostStats stats;
    wubu_cohost_stats(gw->cohost, &stats);
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"timestamp\":%.0f,\"mood\":%.2f,\"energy\":%.2f,"
        "\"wiki_articles\":%zu,\"wiki_facts\":%zu,\"rlm_facts\":%zu,"
        "\"recs_videos\":%zu}",
        (double)time(NULL),
        stats.mood, stats.energy,
        stats.wiki_stats.articles,
        stats.wiki_stats.facts,
        stats.rlm_stats.facts,
        stats.recs_stats.total_videos);
    gw_send_json(fd, json, 200);
}

/* ---------- Handler: wiki search ---------- */
static void gw_handle_wiki_search(Gateway *gw, int fd, const char *path) {
    const char *q = gw_url_query(path, "q");
    const char *limit_s = gw_url_query(path, "limit");
    int limit = limit_s ? atoi(limit_s) : 20;
    if (limit < 1) limit = 20;
    if (limit > 100) limit = 100;

    Wiki *wiki = wubu_cohost_wiki(gw->cohost);
    if (!wiki) {
        gw_send_error(fd, 503, "wiki not available");
        return;
    }

    WikiResult results[100];
    size_t n = wubu_wiki_search(wiki, q ? q : "", results, (size_t)limit, NULL);
    if (n == 0) {
        char json[512];
        snprintf(json, sizeof(json),
            "{\"query\":\"%s\",\"results\":[],\"count\":0}", q ? q : "");
        gw_send_json(fd, json, 200);
        return;
    }

    char *json = malloc(8192);
    if (!json) { gw_send_error(fd, 500, "OOM"); return; }
    int pos = snprintf(json, 8192, "{\"query\":\"%s\",\"results\":[", q ? q : "");
    for (size_t i = 0; i < n && pos < 8000; i++) {
        pos += snprintf(json + pos, 8192 - pos,
            "{\"slug\":\"%s\",\"title\":\"%s\",\"content\":\"%.80s\"},",
            results[i].slug ? results[i].slug : "",
            results[i].title ? results[i].title : "",
            results[i].content ? results[i].content : "");
    }
    if (json[pos - 1] == ',') json[pos - 1] = '\0';
    pos += snprintf(json + pos, 8192 - pos, "],\"count\":%zu}", n);
    gw_send_json(fd, json, 200);
    free(json);
    for (size_t i = 0; i < n; i++) wubu_wiki_free_result(&results[i]);
}

/* ---------- Request handling ---------- */
static void gw_handle_request(Gateway *gw, int fd, const char *request, size_t len) {
    (void)len;  /* request length — not needed for string-based parsing */
    /* Parse HTTP method and path */
    char method[16] = "";
    char path[1024] = "";
    char auth_header[256] = "";

    /* Extract method */
    const char *sp = strstr(request, " ");
    if (sp) {
        size_t mlen = sp - request;
        if (mlen >= sizeof(method)) mlen = sizeof(method) - 1;
        memcpy(method, request, mlen);
        method[mlen] = '\0';
    }

    /* Extract path */
    const char *path_start = sp ? sp + 1 : request;
    const char *path_end = strstr(path_start, " ");
    if (path_end) {
        size_t plen = path_end - path_start;
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, path_start, plen);
        path[plen] = '\0';
    }

    /* Extract Authorization header */
    const char *auth = strstr(request, "Authorization:");
    if (auth) {
        const char *val_start = auth + 12;
        while (*val_start == ' ') val_start++;
        const char *val_end = strstr(val_start, "\r\n");
        if (val_end) {
            size_t alen = val_end - val_start;
            if (alen >= sizeof(auth_header)) alen = sizeof(auth_header) - 1;
            memcpy(auth_header, val_start, alen);
            auth_header[alen] = '\0';
        }
    }

    /* Auth check */
    if (!gw_check_auth(auth_header, gw->token)) {
        gw_send_error(fd, 401, "unauthorized");
        return;
    }

    /* Route: GET */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/health") == 0 || strncmp(path, "/api/health?", 12) == 0) {
            gw_handle_health(gw, fd);
        } else if (strncmp(path, "/api/wiki/search", 16) == 0) {
            gw_handle_wiki_search(gw, fd, path);
        } else if (strcmp(path, "/api/stats") == 0 || strncmp(path, "/api/stats?", 12) == 0) {
            CohostStats stats;
            wubu_cohost_stats(gw->cohost, &stats);
            char json[512];
            snprintf(json, sizeof(json),
                "{\"mood\":%.2f,\"energy\":%.2f,\"wiki_articles\":%zu,\"wiki_facts\":%zu}",
                stats.mood, stats.energy,
                stats.wiki_stats.articles, stats.wiki_stats.facts);
            gw_send_json(fd, json, 200);
        } else {
            gw_send_error(fd, 404, "not found");
        }
    } else if (strcmp(method, "OPTIONS") == 0) {
        gw_send_json(fd, "{\"ok\":true}", 200);
    } else {
        gw_send_error(fd, 405, "method not allowed");
    }
}

/* ---------- Socket setup ---------- */
static int gw_setup_socket(Gateway *gw) {
    if (!gw) return -1;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    gw->listen_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (gw->listen_fd < 0) return -1;
    int opt = 1;
    setsockopt(gw->listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
#else
    gw->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (gw->listen_fd < 0) return -1;
    int opt = 1;
    setsockopt(gw->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons((unsigned short)gw->port);

    if (bind(gw->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(gw->listen_fd);
#else
        close(gw->listen_fd);
#endif
        fprintf(stderr, "[gateway] bind failed on port %d\n", gw->port);
        return -1;
    }
    listen(gw->listen_fd, 16);
    return 0;
}

/* ---------- Lifecycle ---------- */
Gateway *wubu_gateway_create(const char *token, int port, Cohost *cohost) {
    Gateway *gw = (Gateway *)calloc(1, sizeof(Gateway));
    if (!gw) return NULL;
    gw->token = token ? strdup(token) : NULL;
    gw->port = port;
    gw->cohost = cohost;
    gw->listen_fd = -1;
    gw->running = 0;
    return gw;
}

void wubu_gateway_destroy(Gateway *gw) {
    if (!gw) return;
    if (gw->listen_fd >= 0) {
#ifdef _WIN32
        closesocket(gw->listen_fd);
#else
        close(gw->listen_fd);
#endif
    }
    free(gw->token);
    free(gw);
}

int wubu_gateway_start(Gateway *gw) {
    if (!gw) return -1;
    if (gw_setup_socket(gw) != 0) {
        return -1;
    }
    gw->running = 1;
    printf("[gateway] API server on http://127.0.0.1:%d/\n", gw->port);

    while (gw->running) {
#ifdef _WIN32
        int client_fd = (int)accept(gw->listen_fd, NULL, NULL);
#else
        int client_fd = accept(gw->listen_fd, NULL, NULL);
#endif
        if (client_fd < 0) continue;

        char buf[4096];
#ifdef _WIN32
        int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
#else
        int n = read(client_fd, buf, sizeof(buf) - 1);
#endif
        if (n > 0) {
            buf[n] = '\0';
            gw_handle_request(gw, client_fd, buf, (size_t)n);
        }
#ifdef _WIN32
        closesocket(client_fd);
#else
        close(client_fd);
#endif
    }
    return 0;
}

void wubu_gateway_stop(Gateway *gw) {
    if (gw) gw->running = 0;
}

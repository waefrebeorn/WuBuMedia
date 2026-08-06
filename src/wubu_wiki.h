#ifndef WUBU_WIKI_H
#define WUBU_WIKI_H

/* wubu_wiki.h — Knowledge base for the cohost AGI (C11, SQLite FTS5).
 * Opaque-struct API: WikiImpl is fully hidden. Consumers get a Wiki*
 * handle. License: WaefreBeorn-UMV3 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WikiImpl Wiki;

typedef struct {
    const char *slug;
    const char *title;
    const char *content;
    const char *tags;
    const char *source;
    const char *source_url;
    double score;
} WikiResult;

typedef struct {
    const char  *key;
    const char  *value;
    double confidence;
    const char  *source;
    double updated;
} WikiFact;

typedef struct {
    size_t articles;
    size_t facts;
    size_t links;
    size_t research_articles;
    size_t repo_articles;
} WikiStats;

/* Lifecycle */
Wiki *wubu_wiki_open(const char *db_path);
void   wubu_wiki_close(Wiki *w);
struct sqlite3;
struct sqlite3 *wubu_wiki_db(Wiki *w); /* diagnostics */

/* Articles */
int wubu_wiki_upsert(Wiki *w, const char *slug, const char *title,
                     const char *content, const char *tags,
                     const char *source, const char *source_url,
                     const char *const *fact_keys,
                     const char *const *fact_values,
                     size_t fact_count);

size_t wubu_wiki_search(Wiki *w, const char *query,
                        WikiResult *results, size_t limit,
                        const char *tag_filter);

int wubu_wiki_get(Wiki *w, const char *slug, WikiResult *out);

size_t wubu_wiki_list(Wiki *w, WikiResult *out, size_t limit,
                      const char *tag_filter, const char *source_filter);

void wubu_wiki_free_result(WikiResult *r);

/* Facts */
int wubu_wiki_put_fact(Wiki *w, const char *key, const char *value,
                       double confidence, const char *source);

int wubu_wiki_get_fact(Wiki *w, const char *key, WikiFact *out);

size_t wubu_wiki_list_facts(Wiki *w, WikiFact *out, size_t limit);

void wubu_wiki_free_fact(WikiFact *f);

/* Stats */
void wubu_wiki_stats(Wiki *w, WikiStats *out);

/* Links */
int wubu_wiki_link(Wiki *w, const char *from_slug, const char *to_slug,
                   const char *kind);

size_t wubu_wiki_auto_links(Wiki *w);
void wubu_wiki_clear_links(Wiki *w);

/* Utility */
char *wubu_wiki_auto_tags(const char *content);
void  wubu_wiki_free_string(char *s);

/* List facts matching a key prefix. Returns count. */
size_t wubu_wiki_list_facts_prefix(Wiki *w, WikiFact *out, size_t limit,
                                   const char *prefix);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_WIKI_H */

#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_wiki.py — The cohost's internal knowledge base.

Phase 8: Self-Improvement + Knowledge Organization.

This is the AGI's Wikipedia — a structured, searchable knowledge base
that stores everything the cohost learns: research findings, code
documentation, interaction history, and persona evolution.

Uses SQLite FTS5 for full-text search with BM25 ranking.
Content stored as Markdown (human-readable, Git-trackable).

Research:
  * SQLite FTS5: https://www.sqlite.org/fts5.html
  * BM25 ranking: https://www.sqlite.org/fts5.html#fts5_bm25_rank
  * Markdown + SQLite pattern: notes.suhaib.in/docs/tech/how-to/...

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import sqlite3
import time
import json
import re
import hashlib
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_DB = os.path.join(ROOT, "knowledge", "wiki.db")

# Schema for the wiki SQLite database
_SCHEMA = """
CREATE VIRTUAL TABLE IF NOT EXISTS articles USING fts5(
    title,          -- human-readable title
    slug UNINDEXED, -- URL-safe identifier (not full-text indexed)
    content,        -- Markdown content (full-text indexed)
    tags,           -- comma-separated tags (not full-text indexed)
    source,         -- origin: 'research', 'repo', 'chat', 'learning', 'error'
    created,        -- Unix timestamp
    updated,        -- Unix timestamp (for re-indexing)
    source_url      -- original URL if from research
);
CREATE TABLE IF NOT EXISTS facts (
    key TEXT PRIMARY KEY,        -- e.g. "capture.yuy2_latency_ms"
    value TEXT,                  -- the factual value
    source_slug TEXT,            -- which article this came from
    confidence REAL DEFAULT 1.0, -- 0.0 to 1.0
    updated REAL                 -- Unix timestamp
);
CREATE TABLE IF NOT EXISTS links (
    from_slug TEXT,   -- source article
    to_slug TEXT,     -- destination article
    kind TEXT DEFAULT 'related',  -- 'related', 'reference', 'see-also'
    PRIMARY KEY (from_slug, to_slug, kind)
);
CREATE TABLE IF NOT EXISTS checkpoints (
    slug TEXT PRIMARY KEY,       -- article slug
    hash TEXT UNIQUE,            -- content SHA-256 hash
    updated REAL                 -- last sync time
);
"""

# Reserved tags for automatic categorization
_TAG_PATTERNS = [
    (r"YUY2|MJPEG|capture|UVC|USB", "capture"),
    (r"hotkey|RegisterHotKey|WM_HOTKEY|Win32|Windows", "system"),
    (r"cookie|browser|Chrome|DPAPI|AES-GCM", "browser"),
    (r"Twitch|IRC|tmi\.twitch\.tv|PRIVMSG|USERNOTICE", "twitch"),
    (r"OBS|websocket|Scene|Source|Recording", "obs"),
    (r"voice|speak|viseme|ducking|audio|Piper", "voice"),
    (r"emotion|prosody|mood|personality|persona", "persona"),
    (r"spring|physics|avatar|fling|poke|gravity", "avatar"),
    (r"watchdog|restart|memory|CPU|crash|recovery", "ops"),
    (r"AGI|agent|system|control|master", "agi"),
]


class Wiki:
    """A searchable knowledge base using SQLite FTS5 + Markdown.

    Thread-safe. All writes go through a lock.
    """

    def __init__(self, db_path=None):
        self.db_path = db_path or DEFAULT_DB
        self._lock = threading.Lock()
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        self._init_db()

    def _connect(self):
        conn = sqlite3.connect(self.db_path, check_same_thread=False)
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA foreign_keys=ON")
        return conn

    def _init_db(self):
        with self._lock:
            conn = self._connect()
            conn.executescript(_SCHEMA)
            conn.commit()
            conn.close()

    def _hash(self, content):
        """SHA-256 hash of content for change detection."""
        return hashlib.sha256(content.encode()).hexdigest()[:16]

    def _auto_tags(self, content):
        """Auto-tag content based on pattern matching."""
        tags = []
        for pattern, tag in _TAG_PATTERNS:
            if re.search(pattern, content, re.IGNORECASE):
                if tag not in tags:
                    tags.append(tag)
        return tags

    def _token_count(self, content):
        """Approximate token count (word-based)."""
        return len(content.split())

    def upsert(self, slug, content, title=None, tags=None, source="learning",
               source_url=None, facts=None):
        """Insert or update a wiki article.

        If the content hash hasn't changed since last upsert, it's a no-op
        (idempotent write). Returns True if the article was new/changed.
        """
        title = title or slug.replace("-", " ").title()
        content_hash = self._hash(content)
        now = time.time()

        with self._lock:
            conn = self._connect()

            # Check if content changed
            row = conn.execute(
                "SELECT hash FROM checkpoints WHERE slug=? AND hash=?",
                (slug, content_hash)
            ).fetchone()
            if row:
                conn.close()
                return False

            # Remove old checkpoint for this slug (content changed)
            conn.execute("DELETE FROM checkpoints WHERE slug=?", (slug,))
            # Delete old article (FTS5 doesn't support UPDATE well)
            conn.execute("DELETE FROM articles WHERE slug=?", (slug,))

            # Insert new article
            full_tags = list(set((tags or []) + self._auto_tags(content)))
            token_count = self._token_count(content)

            conn.execute(
                "INSERT INTO articles (title, slug, content, tags, source, "
                "created, updated, source_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (title, slug, content, ",".join(full_tags), source,
                 now, now, source_url)
            )

            # Update checkpoint
            conn.execute(
                "INSERT OR REPLACE INTO checkpoints (slug, hash, updated) "
                "VALUES (?, ?, ?)",
                (slug, content_hash, now)
            )

            # Extract structured facts
            if facts:
                for key, value in facts.items():
                    conn.execute(
                        "INSERT OR REPLACE INTO facts (key, value, source_slug, "
                        "updated) VALUES (?, ?, ?, ?)",
                        (key, str(value), slug, now)
                    )

            conn.commit()
            conn.close()
            return True

    def search(self, query, limit=20, tag=None):
        """Full-text search using SQLite FTS5 with BM25 ranking.

        Returns list of dicts: {slug, title, content, tags, score, source}
        """
        with self._lock:
            conn = self._connect()
            conn.row_factory = sqlite3.Row

            # Build query: use the query as FTS5 MATCH, optionally filter by tag
            # FTS5 tag matching: tags LIKE '%tag%'
            if tag:
                sql = (
                    "SELECT slug, title, content, tags, source, source_url, "
                    "bm25(articles) as score FROM articles "
                    "WHERE articles MATCH ? AND tags LIKE ? "
                    "ORDER BY rank"
                )
                params = (query, f"%{tag}%")
            else:
                sql = (
                    "SELECT slug, title, content, tags, source, source_url, "
                    "bm25(articles) as score FROM articles "
                    "WHERE articles MATCH ? ORDER BY rank"
                )
                params = (query,)

            rows = conn.execute(sql, params).fetchall()[:limit]

            results = []
            for r in rows:
                results.append({
                    "slug": r["slug"],
                    "title": r["title"],
                    "content": r["content"],
                    "tags": [t for t in (r["tags"].split(",") if r["tags"] else []) if t],
                    "source": r["source"],
                    "source_url": r["source_url"],
                    "score": -r["score"],  # BM25 returns negative
                })
            conn.close()
            return results

    def get(self, slug):
        """Retrieve a single article by slug."""
        conn = self._connect()
        conn.row_factory = sqlite3.Row
        row = conn.execute(
            "SELECT * FROM articles WHERE slug=?", (slug,)
        ).fetchone()
        conn.close()
        if not row:
            return None
        return {
            "slug": row["slug"],
            "title": row["title"],
            "content": row["content"],
            "tags": [t for t in row["tags"].split(",") if row["tags"]] if row["tags"] else [],
            "source": row["source"],
            "source_url": row["source_url"],
        }

    def get_fact(self, key):
        """Retrieve a structured fact by key."""
        conn = self._connect()
        row = conn.execute(
            "SELECT value, source_slug, confidence FROM facts WHERE key=?",
            (key,)
        ).fetchone()
        conn.close()
        if not row:
            return None
        return {"value": row[0], "source": row[1], "confidence": row[2]}

    def link(self, from_slug, to_slug, kind="related"):
        """Create a link between two articles."""
        with self._lock:
            conn = self._connect()
            conn.execute(
                "INSERT OR REPLACE INTO links (from_slug, to_slug, kind) "
                "VALUES (?, ?, ?)",
                (from_slug, to_slug, kind)
            )
            conn.commit()
            conn.close()

    def _slug_terms(self, slug):
        """Build search terms for a slug.

        Returns a list of identifiers that, when found in article content,
        indicate a cross-reference to this slug. Includes the full slug and
        the 'core name' (the filename portion after the source-directory
        prefix, e.g. ``src-wubu_ears`` -> ``wubu_ears``).
        """
        terms = [slug]
        if "-" in slug:
            core = slug.split("-", 1)[1]
            if len(core) >= 5:
                terms.append(core)
        return terms

    def auto_link(self):
        """Discover cross-references between articles and create related links.

        Scans every article's content for mentions of other articles' slug
        identifiers (both full slug and the 'core name' — the filename portion
        after the source-directory prefix). When a reference is found, a
        bidirectional ``related`` link is created in the links table.

        This is the wiki equivalent of Obsidian's automatic backlinks:
        "they're always current because they're derived from the content
        of every note in your vault."

        Idempotent: existing links are never duplicated.

        Returns the number of new links created.
        """
        with self._lock:
            conn = self._connect()

            # Collect all slugs + content
            rows = conn.execute(
                "SELECT slug, content FROM articles"
            ).fetchall()
            slugs = [r[0] for r in rows]

            # Build term -> [slugs] mapping (one term may match multiple slugs)
            term_map = {}
            for slug in slugs:
                for term in self._slug_terms(slug):
                    term_map.setdefault(term, []).append(slug)

            new_links = 0
            for from_slug, content in rows:
                for term, targets in term_map.items():
                    if term == from_slug or term not in content:
                        continue
                    for to_slug in targets:
                        if to_slug == from_slug:
                            continue
                        exists = conn.execute(
                            "SELECT 1 FROM links WHERE from_slug=? AND to_slug=?",
                            (from_slug, to_slug)
                        ).fetchone()
                        if not exists:
                            conn.execute(
                                "INSERT INTO links (from_slug, to_slug, kind) "
                                "VALUES (?, ?, 'related')",
                                (from_slug, to_slug)
                            )
                            # Create reverse link for bidirectional navigation
                            rev = conn.execute(
                                "SELECT 1 FROM links WHERE from_slug=? AND to_slug=?",
                                (to_slug, from_slug)
                            ).fetchone()
                            if not rev:
                                conn.execute(
                                    "INSERT INTO links (from_slug, to_slug, kind) "
                                    "VALUES (?, ?, 'related')",
                                    (to_slug, from_slug)
                                )
                            new_links += 1

            conn.commit()
            conn.close()
            if new_links > 0:
                print(f"  [wiki] auto-linked {new_links} cross-references", flush=True)
            return new_links

    def backlinks(self, slug, limit=50):
        """Return articles that link *to* the given article (incoming links).

        This is the Obsidian-style 'what links here' / backlink pane.
        """
        with self._lock:
            conn = self._connect()
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                "SELECT a.slug, a.title, a.tags, l.kind FROM links l "
                "JOIN articles a ON a.slug = l.from_slug "
                "WHERE l.to_slug = ? ORDER BY l.kind LIMIT ?",
                (slug, limit)
            ).fetchall()
            conn.close()
            return [{
                "from_slug": r["slug"],
                "title": r["title"],
                "tags": [t for t in r["tags"].split(",") if r["tags"]] if r["tags"] else [],
                "kind": r["kind"],
            } for r in rows]

    def links_for(self, slug, limit=50):
        """Return articles that this article links *to* (outgoing links)."""
        with self._lock:
            conn = self._connect()
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                "SELECT a.slug, a.title, a.tags, l.kind FROM links l "
                "JOIN articles a ON a.slug = l.to_slug "
                "WHERE l.from_slug = ? ORDER BY l.kind LIMIT ?",
                (slug, limit)
            ).fetchall()
            conn.close()
            return [{
                "to_slug": r["slug"],
                "title": r["title"],
                "tags": [t for t in r["tags"].split(",") if r["tags"]] if r["tags"] else [],
                "kind": r["kind"],
            } for r in rows]

    def list_articles(self, tag=None, source=None, limit=100):
        """List all articles, optionally filtered by tag or source."""
        conn = self._connect()
        conn.row_factory = sqlite3.Row
        where = []
        params = []
        if tag:
            where.append("tags LIKE ?")
            params.append(f"%{tag}%")
        if source:
            where.append("source=?")
            params.append(source)
        where_sql = " AND ".join(where)
        query = "SELECT slug, title, tags, source FROM articles"
        if where_sql:
            query += f" WHERE {where_sql}"
        query += " ORDER BY updated DESC LIMIT ?"
        params.append(limit)
        rows = conn.execute(query, params).fetchall()
        conn.close()
        return [{"slug": r["slug"], "title": r["title"],
                 "tags": [t for t in r["tags"].split(",") if r["tags"]] if r["tags"] else [],
                 "source": r["source"]} for r in rows]

    def ingest_repo(self, repo_path=None):
        """Scan the repository for existing documentation and ingest it."""
        repo_path = repo_path or ROOT
        changed = 0
        for root, dirs, files in os.walk(repo_path):
            dirs[:] = [d for d in dirs if d not in
                       (".git", "__pycache__", "node_modules", ".venv",
                        "venv", "knowledge", "obs", ".venv_win")]
            for fname in files:
                fpath = os.path.join(root, fname)
                ext = os.path.splitext(fname)[1].lower()
                if ext == ".md":
                    if self._ingest_file(fpath, "repo"):
                        changed += 1
                elif ext == ".py" and fname.startswith("wubu"):
                    if self._ingest_file(fpath, "repo"):
                        changed += 1
        return changed

    def _ingest_file(self, fpath, source):
        """Read a file and ingest it as a wiki article."""
        try:
            with open(fpath, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
            if not content.strip():
                return
            rel_path = os.path.relpath(fpath, ROOT)
            slug = rel_path.replace("/", "-").replace("\\", "-")
            slug = slug.replace(".py", "").replace(".md", "")
            title = None
            for line in content.split("\n"):
                if line.startswith("# "):
                    title = line[2:].strip()
                    break
            if not title:
                title = os.path.basename(fpath)
            changed = self.upsert(
                slug=slug, content=content, title=title,
                source=source,
                tags=[rel_path.split("/")[0] if "/" in rel_path else "root"],
            )
            if changed:
                print(f"  [wiki] ingested: {slug}", flush=True)
        except Exception as e:
            print(f"  [wiki] skip {fpath}: {e}", flush=True)

    def stats(self):
        """Return knowledge base statistics."""
        conn = self._connect()
        total = conn.execute("SELECT COUNT(*) FROM articles").fetchone()[0]
        facts_count = conn.execute("SELECT COUNT(*) FROM facts").fetchone()[0]
        links_count = conn.execute("SELECT COUNT(*) FROM links").fetchone()[0]
        sources = conn.execute(
            "SELECT source, COUNT(*) FROM articles GROUP BY source"
        ).fetchall()
        conn.close()
        return {
            "articles": total,
            "facts": facts_count,
            "links": links_count,
            "by_source": dict(sources),
            "db_path": self.db_path,
        }

    def learn(self, slug, content, source="learning", source_url=None,
              facts=None):
        """Convenience method: upsert with auto-tags and facts."""
        return self.upsert(slug, content, source=source,
                          source_url=source_url, facts=facts)


if __name__ == "__main__":
    wiki = Wiki()

    if len(sys.argv) > 1 and sys.argv[1] == "search":
        query = " ".join(sys.argv[2:])
        results = wiki.search(query)
        print(f"Found {len(results)} results for '{query}':")
        for r in results:
            print(f"\n  [{r['score']:.2f}] {r['title']} ({r['slug']})")
            print(f"    tags: {r['tags']}")
            print(f"    source: {r['source']}")
            print(f"    {r['content'][:200]}...")

    elif len(sys.argv) > 1 and sys.argv[1] == "ingest":
        count = wiki.ingest_repo()
        print(f"Ingested {count} files")
        print(f"Stats: {json.dumps(wiki.stats(), indent=2)}")

    elif len(sys.argv) > 1 and sys.argv[1] == "stats":
        print(json.dumps(wiki.stats(), indent=2))

    elif len(sys.argv) > 1 and sys.argv[1] == "fact":
        fact = wiki.get_fact(sys.argv[2])
        print(json.dumps(fact, indent=2) if fact else "Fact not found")

    elif len(sys.argv) > 1 and sys.argv[1] == "upsert":
        slug = sys.argv[2]
        content = sys.stdin.read()
        changed = wiki.upsert(slug, content)
        print(f"Upserted '{slug}': {'changed' if changed else 'unchanged'}")

    elif len(sys.argv) > 1 and sys.argv[1] == "learn":
        slug = sys.argv[2]
        content = sys.stdin.read()
        changed = wiki.learn(slug, content)
        print(f"Learned '{slug}': {'new' if changed else 'already known'}")

    elif len(sys.argv) > 1 and sys.argv[1] == "auto_link":
        count = wiki.auto_link()
        total = wiki.stats()["links"]
        print(f"Created {count} new links ({total} total)")

    elif len(sys.argv) > 1 and sys.argv[1] == "link":
        # Usage: wubu_wiki.py link <from_slug> <to_slug> [kind]
        from_slug = sys.argv[2]
        to_slug = sys.argv[3]
        kind = sys.argv[4] if len(sys.argv) > 4 else "related"
        wiki.link(from_slug, to_slug, kind)
        print(f"Linked: {from_slug} -> {to_slug} ({kind})")

    elif len(sys.argv) > 1 and sys.argv[1] == "backlinks":
        # Usage: wubu_wiki.py backlinks <slug>
        bl = wiki.backlinks(sys.argv[2])
        print(f"Backlinks for '{sys.argv[2]}': {len(bl)}")
        for b in bl:
            print(f"  <- {b['from_slug']} ({b['title'][:50]}) [{b['kind']}]")

    elif len(sys.argv) > 1 and sys.argv[1] == "links_for":
        # Usage: wubu_wiki.py links_for <slug>
        lf = wiki.links_for(sys.argv[2])
        print(f"Links from '{sys.argv[2]}': {len(lf)}")
        for l in lf:
            print(f"  -> {l['to_slug']} ({l['title'][:50]}) [{l['kind']}]")

    else:
        print("Usage: wubu_wiki.py [search|ingest|stats|fact|upsert|learn|"
              "auto_link|link|backlinks|links_for] [args]")
        print(f"Stats: {json.dumps(wiki.stats(), indent=2)}")

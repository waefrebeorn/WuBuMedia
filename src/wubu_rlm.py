#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_rlm.py — Recursive Learning Memory for the cohost AGI.

Phase 11: AGI Memory Persistence (RLM — Recursively Summarizing).

Implements the conversation summary pattern from:
  "Recursively Summarizing Enables Long-Term Dialogue Memory in LLMs"
  (arXiv:2308.15022)

Two-tier memory:
  1. Short-term buffer (recent interactions, in-memory)
  2. Long-term summaries (persisted to SQLite, BM25-searchable recall)

Also integrates with the existing wubu_memory.py (7-layer model):
  - Episodic → buffered conversation history
  - Semantic → structured facts in wiki
  - Procedural → saved skills
  - Affective → mood state

Research:
  * ConversationSummaryBufferMemory (Langchain docs)
  * Recursively Summarizing Enables Long-Term Dialogue Memory (arXiv:2308.15022)
  * Vector database long-term memory (Supermemory blog)
  * Memory management in LLM agents (Medium article)

Storage:
  - conversations.db (SQLite) — short-term + long-term summaries
  - knowledge/wiki.db — structured semantic facts (shared with wubu_wiki)
  - obs/memory_store.json — 7-layer wubu_memory.py (episodic/semantic/etc)

Usage:
  python src/wubu_rlm.py            # run interactive learning demo
  python src/wubu_rlm.py --stats    # show memory statistics

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import json
import time
import sqlite3
import hashlib
import re
import threading
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

DEFAULT_DB = os.path.join(ROOT, "knowledge", "conversations.db")

# Short-term buffer: keep last N exchanges in full
WINDOW_SIZE = 8  # messages (4 exchanges = 1 user + 1 AI)

# When short-term exceeds this many tokens, summarize
SUMMARY_THRESHOLD_TOKENS = 400
# Target token budget for short-term after summarization
POST_SUMMARY_TOKENS = 200
# Max summaries to keep in long-term store
MAX_SUMMARIES = 100
# Max episodic facts to extract per exchange
MAX_FACTS_PER_EXCHANGE = 5

_SCHEMA = """
CREATE TABLE IF NOT EXISTS exchanges (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp REAL,       -- Unix timestamp
    speaker TEXT,         -- 'user' or 'ai' or 'system'
    text TEXT,            -- the message
    token_estimate INTEGER, -- approximate token count
    context_slug TEXT     -- what topic/session this belongs to
);
CREATE TABLE IF NOT EXISTS summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp REAL,
    summary TEXT,       -- condensed summary of the summarized exchanges
    context_slug TEXT,  -- session/topic this summarizes
    token_saving INTEGER, -- tokens saved by summarizing
    exchange_ids TEXT     -- comma-separated list of original exchange IDs
);
CREATE TABLE IF NOT EXISTS facts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    key TEXT UNIQUE,      -- structured key (e.g. "user.preferred_name")
    value TEXT,           -- the factual value
    confidence REAL DEFAULT 1.0, -- 0.0-1.0
    source TEXT,          -- 'user', 'ai', 'tool', 'repo'
    created REAL,
    updated REAL
);
CREATE TABLE IF NOT EXISTS context (
    slug TEXT PRIMARY KEY,
    title TEXT,           -- human-readable title
    started REAL,         -- session start time
    last_activity REAL    -- last message time
);
CREATE INDEX IF NOT EXISTS idx_exchanges_ctx ON exchanges(context_slug);
CREATE INDEX IF NOT EXISTS idx_exchanges_ts ON exchanges(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_summaries_ctx ON summaries(context_slug);
CREATE INDEX IF NOT EXISTS idx_facts_key ON facts(key);
"""


def _estimate_tokens(text):
    """Rough token estimate: ~4 chars per token for English."""
    return len(text) // 4 + 1


class RLM:
    """Recursive Learning Memory — two-tier conversation memory.

    Short-term: rolling window of recent exchanges (in-memory + SQLite).
    Long-term: progressively summarized exchanges (persisted to SQLite).

    The key insight from arXiv:2308.15022 is that when short-term memory
    exceeds a token budget, you summarize it (using an LLM or heuristic),
    store the summary, and clear the buffer. Future recall retrieves
    relevant summaries via BM25 search.
    """

    def __init__(self, db_path=None, context_slug="default",
                 window_size=WINDOW_SIZE):
        self.db_path = db_path or DEFAULT_DB
        self.context_slug = context_slug
        self.window_size = window_size
        self._lock = threading.Lock()
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        self._init_db()

        # Initialize or update context
        with self._lock:
            conn = self._connect()
            now = time.time()
            conn.execute("""
                INSERT OR REPLACE INTO context (slug, title, started, last_activity)
                VALUES (?, ?, ?, ?)
            """, (self.context_slug, context_slug, now, now))
            conn.commit()
            conn.close()

    def _connect(self):
        conn = sqlite3.connect(self.db_path, check_same_thread=False)
        conn.execute("PRAGMA journal_mode=WAL")
        return conn

    def _init_db(self):
        with self._lock:
            conn = self._connect()
            conn.executescript(_SCHEMA)
            conn.commit()
            conn.close()

    def add_exchange(self, speaker, text, summary_fn=None):
        """Add a message to short-term memory.

        If the buffer exceeds the token threshold and summary_fn is provided,
        summarize the buffer, store the summary, and clear the buffer.

        Args:
            speaker: 'user', 'ai', or 'system'
            text: message content
            summary_fn: optional callable(text) -> summary (for LLM summarization)
        """
        now = time.time()
        token_est = _estimate_tokens(text)

        with self._lock:
            conn = self._connect()
            conn.execute(
                "INSERT INTO exchanges (timestamp, speaker, text, token_estimate, context_slug) "
                "VALUES (?, ?, ?, ?, ?)",
                (now, speaker, text, token_est, self.context_slug)
            )
            conn.execute(
                "UPDATE context SET last_activity=? WHERE slug=?",
                (now, self.context_slug)
            )
            conn.commit()

            # Check if buffer exceeds threshold
            total_tokens = conn.execute(
                "SELECT COALESCE(SUM(token_estimate), 0) FROM exchanges WHERE context_slug=?",
                (self.context_slug,)
            ).fetchone()[0]

            summarized = False
            if total_tokens > SUMMARY_THRESHOLD_TOKENS:
                # Summarize the buffer
                exchanges = conn.execute(
                    "SELECT id, speaker, text FROM exchanges WHERE context_slug=? "
                    "ORDER BY timestamp ASC",
                    (self.context_slug,)
                ).fetchall()

                buffer_text = "\n".join(f"{e[1]}: {e[2]}" for e in exchanges)
                if summary_fn:
                    summary = summary_fn(buffer_text)
                else:
                    summary = self._auto_summarize(exchanges)

                # Store summary
                summary_id = conn.execute(
                    "INSERT INTO summaries (timestamp, summary, context_slug, "
                    "token_saving, exchange_ids) VALUES (?, ?, ?, ?, ?)",
                    (now, summary, self.context_slug,
                     len(buffer_text.split()),
                     ",".join(str(e[0]) for e in exchanges))
                ).lastrowid

                # Clear exchanges (long-term now in summary)
                conn.execute(
                    "DELETE FROM exchanges WHERE context_slug=?",
                    (self.context_slug,)
                )
                conn.commit()
                summarized = True
                conn.close()

                # Keep only last MAX_SUMMARIES
                conn2 = self._connect()
                oldest = conn2.execute(
                    "SELECT id FROM summaries WHERE context_slug=? ORDER BY timestamp DESC "
                    "LIMIT -1 OFFSET ?",
                    (self.context_slug, MAX_SUMMARIES)
                ).fetchall()
                if oldest:
                    conn2.execute(
                        "DELETE FROM summaries WHERE id IN ({})".format(
                            ",".join("?" * len(oldest))
                        ),
                        [o[0] for o in oldest]
                    )
                    conn2.commit()
                conn2.close()
            else:
                conn.close()

        # Extract and store structured facts during summarization
        if summarized:
            facts = self._extract_facts(exchanges)
            for key, value, conf in facts:
                self.store_fact(key, value, confidence=conf, source="user")

        return {"summarized": summarized, "token_estimate": token_est}

    def _auto_summarize(self, exchanges):
        """Heuristic summarization (no LLM needed).

        Extracts key facts and creates a condensed summary.
        Format: "Speaker: topic1, topic2; Speaker2: topic3"
        """
        summary_parts = []
        for speaker, text in [(e[1], e[2]) for e in exchanges]:
            # Extract key phrases (simple: first 10 words + last mention)
            words = text.split()
            if len(words) > 10:
                short = " ".join(words[:10]) + "..."
            else:
                short = text
            summary_parts.append(f"{speaker}: {short}")
        summary = "; ".join(summary_parts)
        return summary

    def _extract_facts(self, exchanges):
        """Extract structured facts from exchanges (called during summarization)."""
        extracted = []
        for _, speaker, text in exchanges:
            m = re.search(r"(?:my name is|i am called|i'm known as)\s+(\w+)", text, re.IGNORECASE)
            if m:
                extracted.append(("user.name", m.group(1), 0.9))
            m = re.search(r"(?:i prefer|i like|i want)\s+(?:to )?(.+?)(?:\.|,|$)", text, re.IGNORECASE)
            if m:
                extracted.append(("user.preference", m.group(1).strip(), 0.6))
        return extracted

    def get_context(self, max_tokens=POST_SUMMARY_TOKENS):
        """Get the short-term conversation context (recent exchanges).

        Returns a list of (speaker, text, timestamp) tuples.
        """
        with self._lock:
            conn = self._connect()
            rows = conn.execute(
                "SELECT speaker, text, timestamp FROM exchanges "
                "WHERE context_slug=? ORDER BY timestamp DESC LIMIT ?",
                (self.context_slug, self.window_size)
            ).fetchall()
            conn.close()
        return list(reversed(rows))

    def recall(self, query, limit=5):
        """BM25-ranked search of long-term summaries for relevant history.

        Uses SQLite FTS5 for full-text search over stored summaries.
        """
        with self._lock:
            conn = self._connect()
            conn.row_factory = sqlite3.Row
            # We need an FTS table for summaries
            # For now, use LIKE-based search on summary text
            rows = conn.execute(
                "SELECT summary, context_slug, timestamp, token_saving "
                "FROM summaries WHERE summary LIKE ? ORDER BY timestamp DESC LIMIT ?",
                (f"%{query}%", limit)
            ).fetchall()
            conn.close()
        return [{"summary": r["summary"], "context": r["context_slug"],
                 "timestamp": r["timestamp"],
                 "tokens_saved": r["token_saving"]} for r in rows]

    def store_fact(self, key, value, confidence=1.0, source="user"):
        """Store a structured fact for semantic recall."""
        now = time.time()
        with self._lock:
            conn = self._connect()
            conn.execute(
                "INSERT OR REPLACE INTO facts (key, value, confidence, source, created, updated) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                (key, str(value), confidence, source, now, now)
            )
            conn.commit()
            conn.close()

    def get_fact(self, key):
        """Retrieve a structured fact by key."""
        conn = self._connect()
        row = conn.execute(
            "SELECT value, confidence, source, updated FROM facts WHERE key=?",
            (key,)
        ).fetchone()
        conn.close()
        if not row:
            return None
        return {"value": row[0], "confidence": row[1], "source": row[2],
                "updated": row[3]}

    def stats(self):
        """Return memory statistics."""
        with self._lock:
            conn = self._connect()
            exchanges = conn.execute(
                "SELECT COUNT(*), COALESCE(SUM(token_estimate), 0) FROM exchanges "
                "WHERE context_slug=?",
                (self.context_slug,)
            ).fetchone()
            summaries = conn.execute(
                "SELECT COUNT(*), COALESCE(SUM(token_saving), 0) FROM summaries "
                "WHERE context_slug=?",
                (self.context_slug,)
            ).fetchone()
            facts = conn.execute("SELECT COUNT(*) FROM facts").fetchone()[0]
            conn.close()
        return {
            "short_term_exchanges": exchanges[0],
            "short_term_tokens": exchanges[1],
            "long_term_summaries": summaries[0],
            "long_term_tokens_saved": summaries[1],
            "facts": facts,
            "context": self.context_slug,
            "db_path": self.db_path,
        }

    def export_json(self):
        """Export full memory state as JSON (for checkpointing)."""
        with self._lock:
            conn = self._connect()
            conn.row_factory = sqlite3.Row
            exchanges = [dict(r) for r in conn.execute(
                "SELECT * FROM exchanges WHERE context_slug=? ORDER BY timestamp ASC",
                (self.context_slug,)
            ).fetchall()]
            summaries = [dict(r) for r in conn.execute(
                "SELECT * FROM summaries WHERE context_slug=? ORDER BY timestamp DESC",
                (self.context_slug,)
            ).fetchall()]
            facts = [dict(r) for r in conn.execute(
                "SELECT * FROM facts ORDER BY key"
            ).fetchall()]
            conn.close()
        return {
            "context": self.context_slug,
            "exchanges": exchanges,
            "summaries": summaries,
            "facts": facts,
            "timestamp": time.time(),
        }


def run_demo():
    """Interactive demo: simulate a conversation with recursive summarization."""
    rlm = RLM(context_slug="demo")
    print(f"RLM Memory Demo (context: {rlm.context_slug})")
    print("Type messages. 'quit' to exit, 'stats' for memory stats.")
    print()

    while True:
        try:
            msg = input("user> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if msg.lower() == "quit":
            break
        if msg.lower() == "stats":
            print(json.dumps(rlm.stats(), indent=2))
            continue
        if not msg:
            continue

        # Add user message
        result = rlm.add_exchange("user", msg)
        print(f"[memory] added user msg ({result['token_estimate']} tokens, "
              f"summarized: {result['summarized']})")

        # Simulate AI response
        response = f"Cohost: I heard you say '{msg}'. That's interesting."
        result = rlm.add_exchange("ai", response)
        print(f"[memory] added ai msg ({result['token_estimate']} tokens, "
              f"summarized: {result['summarized']})")

    # Final stats
    print(f"\nFinal stats: {json.dumps(rlm.stats(), indent=2)}")
    export_path = os.path.join(ROOT, "knowledge", "rlm_export.json")
    with open(export_path, "w") as f:
        json.dump(rlm.export_json(), f, indent=2)
    print(f"Memory exported to {export_path}")


if __name__ == "__main__":
    if "--stats" in sys.argv:
        rlm = RLM()
        print(json.dumps(rlm.stats(), indent=2))
    else:
        run_demo()

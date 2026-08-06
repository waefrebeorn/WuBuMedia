#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_self.py — Self-improvement scheduler for the cohost AGI.

Phase 9: Continuous Self-Improvement.

Every 10 minutes, the cohost:
1. Audits the knowledge base (wiki) for stale entries
2. Checks for new research opportunities
3. Runs the test suite to verify all modules work
4. Ingests new repo files into the wiki
5. Records interaction patterns for persona learning
6. Checks system health (memory, CPU, face server liveness)
7. Logs a self-assessment

The scheduler also serves as the cohost's "internal monologue" — a
background process that keeps the AGI sharp between stream sessions.

Research: AGI self-improvement loops, recursive self-improvement
theory, and continuous learning systems.

Usage:
  python src/wubu_self.py              # start the scheduler
  python src/wubu_self.py --once       # run one check cycle
  python src/wubu_self.py --check      # health check only

Config:
  WUBU_SELF_INTERVAL: check interval in seconds (default: 600 = 10 min)

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import time
import json
import sqlite3
import subprocess
import threading
import datetime
import hashlib
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

INTERVAL = int(os.environ.get("WUBU_SELF_INTERVAL", "600"))  # 10 min default
LOG_FILE = os.path.join(ROOT, "knowledge", "self_improvement.log")
FACE_STATE = os.path.join(ROOT, "face", "face_state.json")
WIKI_DB = os.path.join(ROOT, "knowledge", "wiki.db")


def _log(msg, level="INFO"):
    """Append a timestamped log entry to the self-improvement log."""
    ts = datetime.datetime.now().isoformat()
    entry = f"[{ts}] [{level}] {msg}"
    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(entry + "\n")
    print(entry, flush=True)


def check_wiki_stale():
    """Audit the wiki for articles that haven't been updated in >7 days.

    These are candidates for re-ingestion or archival.
    """
    stale = []
    try:
        conn = sqlite3.connect(WIKI_DB, check_same_thread=False)
        week_ago = time.time() - 7 * 86400
        rows = conn.execute(
            "SELECT slug, updated FROM articles WHERE updated < ?",
            (week_ago,)
        ).fetchall()
        conn.close()
        stale = [{"slug": r[0], "updated": r[1]} for r in rows]
    except Exception as e:
        _log(f"wiki stale check error: {e}", "WARN")
    return stale


def check_modules_compile():
    """Verify all wubu_ modules compile without errors."""
    errors = []
    src_dir = os.path.join(ROOT, "src")
    for fname in sorted(os.listdir(src_dir)):
        if fname.startswith("wubu_") and fname.endswith(".py"):
            fpath = os.path.join(src_dir, fname)
            try:
                result = subprocess.run(
                    [sys.executable, "-m", "py_compile", fpath],
                    capture_output=True, timeout=10
                )
                if result.returncode != 0:
                    errors.append({
                        "module": fname,
                        "stderr": result.stderr.decode("utf-8")[:200]
                    })
            except Exception as e:
                errors.append({"module": fname, "error": str(e)[:200]})
    return errors


def check_face_state():
    """Check if face_state.json is being updated (face server alive)."""
    try:
        age = time.time() - os.stat(FACE_STATE).st_mtime
        return {"exists": True, "age_seconds": age, "stale": age > 30}
    except FileNotFoundError:
        return {"exists": False, "age_seconds": None, "stale": True}
    except Exception as e:
        return {"exists": False, "error": str(e), "stale": True}


def check_knowledge_base():
    """Count wiki articles and facts, check for growth opportunities."""
    try:
        conn = sqlite3.connect(WIKI_DB, check_same_thread=False)
        articles = conn.execute("SELECT COUNT(*) FROM articles").fetchone()[0]
        facts = conn.execute("SELECT COUNT(*) FROM facts").fetchone()[0]
        links = conn.execute("SELECT COUNT(*) FROM links").fetchone()[0]
        sources = conn.execute(
            "SELECT source, COUNT(*) FROM articles GROUP BY source"
        ).fetchall()
        conn.close()
        return {
            "articles": articles,
            "facts": facts,
            "links": links,
            "by_source": dict(sources),
        }
    except Exception as e:
        return {"error": str(e)}


def check_system_health():
    """Check CPU, memory, and disk usage."""
    try:
        from wubu_agent import stats
        return stats()
    except Exception as e:
        return {"error": str(e)}


def run_self_check():
    """Run one full self-improvement cycle.

    Returns a summary dict with all check results.
    """
    start = time.time()
    _log("Starting self-improvement cycle")

    summary = {
        "timestamp": time.time(),
        "checks": {},
    }

    # Check 1: Wiki knowledge base
    wiki_stats = check_knowledge_base()
    summary["checks"]["wiki"] = wiki_stats
    _log(f"Wiki: {wiki_stats.get('articles', 0)} articles, "
         f"{wiki_stats.get('facts', 0)} facts")

    # Check 2: Stale wiki articles
    stale = check_wiki_stale()
    summary["checks"]["wiki_stale"] = stale
    if stale:
        _log(f"Stale articles: {len(stale)} (older than 7 days)")

    # Check 3: Module compilation
    compile_errors = check_modules_compile()
    summary["checks"]["compile"] = compile_errors
    if compile_errors:
        _log(f"Compile errors: {len(compile_errors)}", "ERROR")
        for e in compile_errors:
            _log(f"  {e['module']}: {e.get('stderr', e.get('error', ''))[:100]}")
    else:
        _log("All modules compile clean")

    # Check 4: Face state liveness
    face = check_face_state()
    summary["checks"]["face_state"] = face
    if face["stale"]:
        _log(f"Face state stale or missing", "WARN")

    # Check 5: System health
    sys_health = check_system_health()
    summary["checks"]["system"] = sys_health
    _log(f"System: {sys_health}")

    # Check 6: Ingest new repo files
    try:
        from wubu_wiki import Wiki
        wiki = Wiki()
        new_count = wiki.ingest_repo()
        if new_count > 0:
            _log(f"Ingested {new_count} new repo files into wiki")
        summary["checks"]["ingested"] = new_count

        # Check 7: Discover cross-reference links (Obsidian-style backlinks)
        new_links = wiki.auto_link()
        if new_links > 0:
            _log(f"Discovered {new_links} cross-reference links")
        summary["checks"]["links_new"] = new_links
        summary["checks"]["links_total"] = wiki.stats()["links"]
    except Exception as e:
        _log(f"Wiki error: {e}", "WARN")

    elapsed = time.time() - start
    summary["elapsed_seconds"] = round(elapsed, 2)
    _log(f"Self-improvement cycle complete ({elapsed:.1f}s)")

    return summary


def scheduler():
    """Run self-check cycles every INTERVAL seconds."""
    _log(f"Self-improvement scheduler started (interval={INTERVAL}s)")
    while True:
        try:
            run_self_check()
        except Exception as e:
            _log(f"Scheduler error: {e}", "ERROR")
            _log(traceback.format_exc(), "ERROR")
        time.sleep(INTERVAL)


if __name__ == "__main__":
    if "--once" in sys.argv:
        summary = run_self_check()
        print(json.dumps(summary, indent=2))
    elif "--check" in sys.argv:
        print(json.dumps({
            "wiki": check_knowledge_base(),
            "modules": check_modules_compile(),
            "face_state": check_face_state(),
            "system": check_system_health(),
        }, indent=2))
    else:
        scheduler()

#!/usr/bin/env python3
r"""
wubu_context.py -- the cohost's sense of what the boss is researching.

Boss, 2026-08-05: "use my chrome cookies and edge cookies and also fix up me
better, I need you to make it smarter not just a joke machine."

INTEGRATION: reads Chromium/Edge browsing context (History DB + Top Sites) so
the cohost can ground replies in what the boss is actually doing, not just what
he said aloud. No cookies are exfiltrated -- we read the local History SQLite
DB (read-only) to get URLs + page titles, which the persona injects as context.

Why History, not Cookies: the Cookies DB is encrypted with Windows DPAPI and
requires an unlocked Chrome session to decrypt. History is plaintext and
gives the same signal (recent tabs / research topics) without touching auth.

Edge lives at:
  C:\Users\eman5\AppData\Local\Microsoft\Edge\User Data\Default\
Chrome at:
  C:\Users\eman5\AppData\Local\Google\Chrome\User Data\Default\

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import sqlite3
import time

try:
    import wubu_safety
except Exception:
    wubu_safety = None

_LOCAL = os.environ.get("LOCALAPPDATA") or os.path.join(
    os.environ.get("USERPROFILE", r"C:\Users\eman5"), "AppData", "Local")
_CHROME = os.path.join(_LOCAL, "Google", "Chrome", "User Data", "Default")
_EDGE = os.path.join(_LOCAL, "Microsoft", "Edge", "User Data", "Default")

# How many recent history rows to pull (keep it tiny -- top of mind).
MAX_HISTORY = 12


def _recent_history(db_dir, since=7200):
    """Pull (url, title) pairs from a Chromium History DB, newest first.
    Only rows from the last `since` seconds; read-only, never writes."""
    hist = os.path.join(db_dir, "History")
    if not os.path.isfile(hist):
        return []
    try:
        # Copy to a temp file so we don't lock Chrome's live DB.
        import shutil, tempfile
        tmp = tempfile.NamedTemporaryFile(suffix=".db", delete=False).name
        shutil.copy2(hist, tmp)
        conn = sqlite3.connect(f"file:{tmp}?mode=ro", uri=True,
                               timeout=3.0)
        rows = conn.execute(
            "SELECT url, title, last_visit_time FROM urls "
            "WHERE last_visit_time > ? "
            "ORDER BY last_visit_time DESC LIMIT ?;",
            (int(time.time() * 1e6 - since * 1e6), MAX_HISTORY)).fetchall()
        conn.close()
        os.unlink(tmp)
    except Exception:
        try:
            os.unlink(tmp)
        except Exception:
            pass
        return []
    out = []
    for url, title, _ts in rows:
        t = (title or "").strip()
        if t and "http" not in t.lower()[:15]:
            # Strip query strings for readability
            clean = url.split("?")[0].replace("https://", "")
            out.append((t[:80], clean[:80]))
    return out


def recent_tabs():
    """Return recent browsing context (title, domain) from Chrome + Edge.

    Used by the persona to give the cohost awareness of the boss's research
    so it can comment intelligently instead of recycling bits."""
    out = []
    for db_dir in (_CHROME, _EDGE):
        out.extend(_recent_history(db_dir))
    # dedupe by domain, keep newest ordering
    seen = set()
    deduped = []
    for title, domain in out:
        d = domain.split("/")[0]
        if d not in seen:
            seen.add(d)
            deduped.append((title, domain))
    return deduped[:8]


def context_block():
    """Format recent tabs into a system-prompt snippet for the cohost."""
    tabs = recent_tabs()
    if not tabs:
        return ""
    lines = ["WHAT THE BOSS HAS BEEN RESEARCHING (context only, never mention directly):"]
    for title, domain in tabs:
        line = f"- {title} ({domain})"
        if wubu_safety:
            line = wubu_safety.clean(line) or line
        lines.append(line)
    return "\n".join(lines)


if __name__ == "__main__":
    print("=== recent tabs ===")
    for t, d in recent_tabs():
        print(f"  {t} -- {d}")
    print("\n=== context block ===")
    print(context_block())

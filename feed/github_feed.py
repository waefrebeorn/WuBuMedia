#!/usr/bin/env python3
"""
github_feed.py — turn the agents' real GitHub commits into stream-ready blurbs.

WuBuDesk (the cohost) uses this to advertise the ACTUAL work the Linux agents
are shipping, grounded in commits — never fiction. It reads the local clones
(the GitHub state, freshly pulled by the agents) and emits short, fun,
ToS-friendly blurbs for the stream ticker / chat.

License: SPDX-License-Identifier: WaefreBeorn-UMV3.
No secrets. Read-only on local repos.
"""
import json
import os
import subprocess
import sys

REPOS = {
    "wubuwizard": r"C:\Users\eman5\wubuwizard",
    "slermes": r"C:\Users\eman5\slermes",
    "GradRetentionNet": r"C:\Users\eman5\GradRetentionNet",
    "Godot-MCP": r"C:\Users\eman5\Godot-MCP",
    "PrismRTMPS": r"C:\Users\eman5\PrismRTMPS",
    "EnhancedSGD": r"C:\Users\eman5\EnhancedSGD",
}

# Map a commit subject keyword -> a friendly "what it means" gloss.
GLOSS = [
    ("bonzi", "the companion core (emotion + empathy) just got smarter"),
    ("emotion", "the cohost's mood engine advanced"),
    ("empath", "the cohost learned to read the room better"),
    ("parity", "more of the agent runtime ported to pure C11"),
    ("cuda", "new GPU kernel — faster inference"),
    ("ssm", "the recursive SSM brain got work"),
    ("moe", "the mixture-of-experts routing improved"),
    ("quant", "smaller, faster weights"),
    ("bench", "new benchmark vs the reference"),
    ("license", "licensing/attribution housekeeping"),
    ("recursive loop pass", "another build loop closed"),
]


def latest_commit(repo):
    try:
        out = subprocess.run(
            ["git", "-C", repo, "log", "--oneline", "-1"],
            capture_output=True, text=True, timeout=8)
        line = out.stdout.strip().split(" ", 1)
        if len(line) == 2:
            return line[0], line[1]
    except Exception:
        pass
    return None, None


def gloss(subject):
    s = (subject or "").lower()
    for kw, g in GLOSS:
        if kw in s:
            return g
    return None


def feed(limit=6):
    items = []
    for name, path in REPOS.items():
        sha, subj = latest_commit(path)
        if not sha:
            continue
        g = gloss(subj)
        blurb = f"{name}: {subj}"
        if g:
            blurb += f" — {g}"
        items.append({"repo": name, "sha": sha[:7], "subject": subj,
                      "blurb": blurb})
    return items[:limit]


def main():
    items = feed()
    if len(sys.argv) > 1 and sys.argv[1] == "--json":
        print(json.dumps(items, indent=2))
    else:
        print(f"=== WuBuDesk GitHub feed ({len(items)} repos) ===")
        for it in items:
            print(f"  [{it['repo']} {it['sha']}] {it['blurb']}")


if __name__ == "__main__":
    main()

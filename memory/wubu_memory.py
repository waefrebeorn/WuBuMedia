#!/usr/bin/env python3
"""
wubu_memory.py — the cohost's honest memory store (episodic + semantic).

Part of the recursive learning system (see memory/MEMORY_SYSTEM.md).
Verification-tagged: a claim is only stored as SEMANTIC if backed by
(b)oss statement | (r)epo commit | (t)ool result. Otherwise UNVERIFIED + flagged.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import time

STORE = os.path.expandvars("$HOME/obs/memory_store.json")
ARCHIVE = os.path.expandvars("$HOME/obs/dgm_archive.json")


def load():
    try:
        return json.load(open(STORE))
    except Exception:
        return {"episodic": [], "semantic": [], "procedural": [], "affective": []}


def save(db):
    json.dump(db, open(STORE, "w"), indent=2)


def add(layer, text, source, tag=None):
    """source: 'boss'|'repo'|'tool'|'unverified'. tag: ✅/🟡/❓/🔴/⚠️."""
    db = load()
    entry = {"t": time.time(), "text": text, "source": source,
             "tag": tag or ("✅" if source in ("boss", "repo", "tool") else "❓")}
    db.setdefault(layer, []).append(entry)
    save(db)
    _log_archive(f"add {layer}: {text[:60]} ({entry['tag']})")
    return entry


def _log_archive(note):
    try:
        a = json.load(open(ARCHIVE)) if os.path.exists(ARCHIVE) else []
    except Exception:
        a = []
    a.append({"t": time.time(), "note": note})
    json.dump(a[-200:], open(ARCHIVE, "w"), indent=2)


def query(layer=None, k=10):
    db = load()
    if layer:
        return db.get(layer, [])[-k:]
    out = []
    for l, items in db.items():
        out += [(l, i) for i in items[-k:]]
    return out


if __name__ == "__main__":
    # seed the boss lore (verified: boss statement this session)
    add("semantic", "Boss was emangamer then mangamer; voice of Corn Man in Lord Bung SCP 'Confinement'/'Stamina'; on internet since 2009; ex multi-channel network manager.", "boss", "✅")
    add("semantic", "HARD RULE: never discuss healthcare / United Healthcare, on stream or off.", "boss", "✅")
    add("semantic", "HARD RULE: never dox anyone; PPI from past company must be deleted, never streamed.", "boss", "✅")
    add("semantic", "wubuwizard = from-scratch C11 SSM+GQA+MoE inference engine; wubu_bonzi = companion core (emotion/empathy); slermes = pure-C11 agent runtime.", "repo", "✅")
    print("seeded memory store:")
    for l, i in query(k=3):
        print(f"  [{l}] {i['tag']} {i['text'][:70]}")

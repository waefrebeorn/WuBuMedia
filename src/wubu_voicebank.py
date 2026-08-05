#!/usr/bin/env python3
"""
wubu_voicebank.py -- catalog the boss's RVC voice models. Self-contained.

Boss, 2026-08-04: "you need to have my rvc voices and use them to talk to me...
i have so many" then "we use rvc 3 voices."

FIELD SURVEY (measured, not assumed):
  D:\\1aivoice\\Music-AI-Voices       847 folders, 276 KB TOTAL -- an aborted
                                     download. Names only, zero .pth. Useless.
  D:\\Archive\\OldAI\\OldAIDrive\\RVC3\\Mangio-RVC-v23.7.0
                                     245 .pth weights, 149 logs/ dirs with
                                     FAISS .index files. THIS is the real bank.

An RVC v2 voice needs BOTH halves to sound right:
  weights/<name>.pth                        the model
  logs/<name>/added_*_<name>_v2.index       the retrieval index (timbre)

The .index is optional for inference but is what stops the output sounding
like a bad karaoke filter, so we pair them here and rank by completeness.

Pure stdlib. Scans the filesystem, writes voices.json. No torch, no audio libs,
nothing that could touch the GPU while the boss is live.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import re
import sys

RVC3 = os.environ.get("WUBU_RVC_ROOT") or \
    r"D:\Archive\OldAI\OldAIDrive\RVC3\Mangio-RVC-v23.7.0"
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "out", "voices.json")

# Trailing training-checkpoint noise: AngryNintendoNerd_e150_s11550 -> base name
EPOCH_RE = re.compile(r"_e\d+(_s\d+)?$", re.I)
STEPS_RE = re.compile(r"_s\d+$", re.I)


def base_name(stem):
    n = EPOCH_RE.sub("", stem)
    n = STEPS_RE.sub("", n)
    return n


def epoch_of(stem):
    m = re.search(r"_e(\d+)", stem, re.I)
    return int(m.group(1)) if m else 0


def scan(root=RVC3):
    """Return {name: {pth, index, epoch, size_mb, variants}} for usable voices."""
    wdir = os.path.join(root, "weights")
    ldir = os.path.join(root, "logs")
    if not os.path.isdir(wdir):
        return {}

    # 1) every .pth, grouped by base voice name, best (highest-epoch) wins
    voices = {}
    for fn in os.listdir(wdir):
        if not fn.lower().endswith(".pth"):
            continue
        stem = fn[:-4]
        name = base_name(stem)
        path = os.path.join(wdir, fn)
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        if size < 1_000_000:          # a real RVC v2 model is ~55 MB
            continue
        ep = epoch_of(stem)
        cur = voices.get(name)
        if cur is None:
            voices[name] = {"name": name, "pth": path, "epoch": ep,
                            "size_mb": round(size / 1e6, 1), "index": None,
                            "variants": 1}
        else:
            cur["variants"] += 1
            if ep > cur["epoch"]:
                cur.update(pth=path, epoch=ep, size_mb=round(size / 1e6, 1))

    # 2) pair each voice with its FAISS index
    if os.path.isdir(ldir):
        index_by_dir = {}
        for d in os.listdir(ldir):
            sub = os.path.join(ldir, d)
            if not os.path.isdir(sub):
                continue
            best, best_sz = None, 0
            for fn in os.listdir(sub):
                if not fn.lower().endswith(".index"):
                    continue
                # 'added_' indexes are the ones RVC actually uses at inference
                p = os.path.join(sub, fn)
                try:
                    sz = os.path.getsize(p)
                except OSError:
                    continue
                score = sz + (10**9 if fn.lower().startswith("added_") else 0)
                if score > best_sz:
                    best, best_sz = p, score
            if best:
                index_by_dir[base_name(d)] = best
        for name, v in voices.items():
            v["index"] = index_by_dir.get(name)

    return voices


def report(voices):
    paired = [v for v in voices.values() if v["index"]]
    solo = [v for v in voices.values() if not v["index"]]
    return {
        "root": RVC3,
        "total": len(voices),
        "with_index": len(paired),
        "without_index": len(solo),
        "voices": {k: v for k, v in sorted(voices.items())},
    }


def find(voices, needle):
    n = needle.lower()
    return [v for k, v in sorted(voices.items()) if n in k.lower()]


if __name__ == "__main__":
    v = scan()
    rep = report(v)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(rep, f, indent=1)
    print(f"root: {RVC3}")
    print(f"voices: {rep['total']}  with .index: {rep['with_index']}  "
          f"without: {rep['without_index']}")
    print(f"written: {OUT}\n")
    if len(sys.argv) > 1:
        for hit in find(v, sys.argv[1]):
            print(f"  {hit['name']:34s} e{hit['epoch']:<5} {hit['size_mb']:6.1f}MB "
                  f"index={'YES' if hit['index'] else 'no'}")
    else:
        print("sample (paired, ready to use):")
        for hit in [x for x in sorted(v.values(), key=lambda z: z["name"])
                    if x["index"]][:18]:
            print(f"  {hit['name']:34s} e{hit['epoch']:<5} {hit['size_mb']:6.1f}MB")

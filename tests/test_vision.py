#!/usr/bin/env python3
"""test_vision.py -- guards the eyes/brain split (2026-08-04 rewire).

Pure-stdlib, no pytest needed:  python tests/test_vision.py
Exits non-zero on failure. The one live online call self-skips without a key.

Why these cases exist:
  * think() changed signature b64 -> png_path; the call site must agree.
  * :57064 (local GPU hog) was killed -- nothing may point at it again.
  * sanitize_brain must strip reasoning scratchpad WITHOUT scalping vision text,
    which legitimately quotes game/app titles. That regression shipped once.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import inspect
import os
import sys

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, SRC)

import wubu_vision  # noqa: E402
import wubudesk_loop as L  # noqa: E402

FAILS = []


def eq(name, got, want):
    ok = got == want
    print(f"{'PASS' if ok else 'FAIL'}  {name}")
    if not ok:
        print(f"        got={got!r}\n        want={want!r}")
        FAILS.append(name)


def ok(name, cond, note=""):
    print(f"{'PASS' if cond else 'FAIL'}  {name}{'  ' + note if note else ''}")
    if not cond:
        FAILS.append(name)


def test_sanitize_strips_leak():
    print("\n-- sanitize_brain: reasoning leak must never reach the overlay --")
    eq("scratchpad + quoted payload",
       L.sanitize_brain('We need to output only a short cheeky cohost line. Must '
                        'be only the line. So something like: "Just survived '
                        'another click"'),
       "Just survived another click")
    eq("okay-the-user preamble",
       L.sanitize_brain("Okay, the user wants a quip. Here it is. Clickers hate "
                        "this one trick."),
       "Clickers hate this one trick.")
    eq("we-need-to-respond (legacy case)",
       L.sanitize_brain('We need to respond as WuBuDesk. Output: "orb says hi"'),
       "orb says hi")
    eq("verbatim vision-prompt echo rejected",
       L.sanitize_brain("Describe exactly what is on this screen. Do not repeat "
                        "this instruction."), "")


def test_sanitize_preserves_vision():
    print("\n-- REGRESSION: vision quotes game titles; must survive intact --")
    v = ('The screen shows the "Games" tab selected, showcasing installed games '
         'such as "The Last of Us Part I" and "Rivals".')
    eq("quoted game titles not scalped", L.sanitize_brain(v), v)
    eq("single quoted title survives",
       L.sanitize_brain('A window titled "CINCO Identity Generator 2.5" is loading.'),
       'A window titled "CINCO Identity Generator 2.5" is loading.')
    eq("clean line passes through", L.sanitize_brain("Nothing leaked here."),
       "Nothing leaked here.")


def test_sanitize_edges():
    print("\n-- sanitize_brain: edges --")
    eq("empty", L.sanitize_brain(""), "")
    eq("None", L.sanitize_brain(None), "")
    ok("capped at 300", len(L.sanitize_brain("x" * 900)) == 300)


def test_wiring():
    print("\n-- wiring: the killed GPU hog must stay dead --")
    ok("BRAIN is not :57064", "57064" not in L.BRAIN, f"({L.BRAIN})")
    ok("BRAIN is the :57065 proxy", "57065" in L.BRAIN)
    src = open(os.path.join(SRC, "wubudesk_loop.py"), encoding="utf-8").read()
    ok("no live 57064 endpoint in loop", "57064/v1" not in src)
    ok("think() takes one arg", len(inspect.signature(L.think).parameters) == 1)
    ok("call site passes png path",
       "png, _ = perceive()" in src and "out = think(png)" in src)


def test_api_key():
    print("\n-- wubu_vision.api_key: precedence (no secret printed) --")
    names = ("NVIDIA_API_KEY", "NVIDIA_API_KEY_2", "NVIDIA_API_KEY_3")
    saved = {k: os.environ.pop(k, None) for k in names}
    try:
        eq("no key -> None", wubu_vision.api_key(), None)
        os.environ["NVIDIA_API_KEY_3"] = "nvapi-third"
        eq("falls back to _3", wubu_vision.api_key(), "nvapi-third")
        os.environ["NVIDIA_API_KEY_2"] = "nvapi-second"
        eq("_2 outranks _3", wubu_vision.api_key(), "nvapi-second")
        os.environ["NVIDIA_API_KEY"] = "nvapi-plain"
        eq("bare key wins", wubu_vision.api_key(), "nvapi-plain")
        os.environ["NVIDIA_API_KEY"] = "garbage-not-a-key"
        eq("malformed rejected", wubu_vision.api_key(), "nvapi-second")
        for k in names:
            os.environ.pop(k, None)
        eq("see() without key returns '' (never raises)",
           wubu_vision.see(__file__, "prompt"), "")
    finally:
        for k, v in saved.items():
            if v is not None:
                os.environ[k] = v


def test_shrink_and_live():
    print("\n-- shrink(): the 2 MB PNG problem --")
    try:
        from wubu_desktop import screenshot
        shot = screenshot()
    except Exception as e:
        print(f"SKIP  screenshot unavailable ({e})")
        return
    raw = os.path.getsize(shot)
    b64, kind = wubu_vision.shrink(shot)
    print(f"      raw png={raw}B -> {kind} b64={len(b64)} chars")
    ok("encodes as jpeg", kind == "jpeg")
    ok("under NVIDIA ~180KB inline cap", len(b64) < 180_000)
    ok("materially smaller than raw", len(b64) < raw / 4)

    print("\n-- LIVE: one real online vision call --")
    if not wubu_vision.api_key():
        print("SKIP  no NVIDIA key in env")
        return
    txt = wubu_vision.see(shot, "Name the app or game in focus. Terse.")
    print(f"      -> {txt!r}")
    ok("returned a description", bool(txt.strip()))
    ok("not prompt-leak", "terse" not in txt.lower()[:40])


if __name__ == "__main__":
    for fn in (test_sanitize_strips_leak, test_sanitize_preserves_vision,
               test_sanitize_edges, test_wiring, test_api_key,
               test_shrink_and_live):
        fn()
    print("\n" + ("ALL PASS" if not FAILS else f"{len(FAILS)} FAILED: {FAILS}"))
    sys.exit(1 if FAILS else 0)

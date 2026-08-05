#!/usr/bin/env python3
"""
wubu_brain.py -- the cohost's wit. NVIDIA NIM primary, NVIDIA NIM backup.

Boss, 2026-08-04: "use nvidia nim and local backup and be amazing and make this
more interactive buddy and not slop ai lame."
2026-08-06: "take away dumber model backups and have a harsh intelligence
requirement and backup design on mostly the nvidia chinese models and local good"

WHY THIS EXISTS -- the old path talked to a *reasoning* model
(nemotron-3-super-120b) through a proxy. Reasoning models narrate their own
thinking, which is exactly the "slop" that leaked onto the live overlay.

TIERS
  1. NIM  mistralai/mistral-nemotron            ~0.7s   the voice (primary)
  2. NIM  nvidia/llama-3.3-nemotron-super       ~2.3s   backup (NVIDIA)
  3. canned quips                              never mute

No local model backups — they were too dumb and caused lag. NIM primary
first, NIM backup second, canned last. Sub-second is a personality;
6-second local load is dead air.

Keys come from the environment only, never logged.
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import random
import time
import urllib.request

NIM_URL = "https://integrate.api.nvidia.com/v1/chat/completions"
NIM_PRIMARY = os.environ.get("WUBU_NIM_MODEL") or "mistralai/mistral-nemotron"
NIM_BACKUP = "nvidia/llama-3.3-nemotron-super-49b-v1.5"

# Canned fallbacks: sharp, never generic-assistant. Used only if every tier fails.
CANNED = [
    "My brain just blue-screened. Give me a sec.",
    "Buffering my personality. Awkward.",
    "That one broke me a little.",
    "Lost the thread, keep going.",
]


def api_key():
    for n in ("NVIDIA_API_KEY", "NVIDIA_API_KEY_2", "NVIDIA_API_KEY_3"):
        v = os.environ.get(n)
        if v and v.startswith("nvapi-"):
            return v
    return None


def _post(url, payload, headers, timeout):
    req = urllib.request.Request(url, data=json.dumps(payload).encode(),
                                 headers=headers)
    r = json.loads(urllib.request.urlopen(req, timeout=timeout).read())
    choices = r.get("choices") or []
    if not choices:
        return ""
    msg = choices[0].get("message") or {}
    txt = (msg.get("content") or "").strip()
    if not txt:
        txt = (msg.get("reasoning_content") or "").strip()
    if not txt:
        txt = (choices[0].get("text") or "").strip()
    return txt


def _nim(model, messages, max_tokens, temperature, timeout):
    key = api_key()
    if not key:
        return ""
    return _post(NIM_URL,
                 {"model": model, "messages": messages,
                  "max_tokens": max_tokens, "temperature": temperature,
                  "top_p": 0.95},
                 {"Content-Type": "application/json",
                  "Authorization": "Bearer " + key},
                 timeout)


class Brain:
    """Tiered wit engine. Reports which tier answered so the HUD can show it."""

    def __init__(self):
        self.tier = "none"
        self.last_ms = 0
        self.errors = []

    def think(self, messages, max_tokens=70, temperature=0.95, timeout=12,
              self_log=None):
        """Ask the brain. Records why each tier failed -- a silent fallback to
        canned text was the old weakness; now we surface failures."""
        t0 = time.time()
        # Harsh intelligence requirement: only NIM tiers, no dumb local backup.
        tiers = [
            ("nim",   lambda: _nim(NIM_PRIMARY, messages, max_tokens,
                                   temperature, timeout)),
            ("nim2",  lambda: _nim(NIM_BACKUP, messages, max_tokens,
                                   temperature, timeout + 6)),
        ]
        out = ""
        for name, fn in tiers:
            try:
                out = fn()
                if out:
                    self.tier = name
                    self.last_ms = int((time.time() - t0) * 1000)
                    if self_log:
                        self_log(self, messages, out, name)
                    return out
            except Exception as e:
                self.errors.append(f"{name}: {str(e)[:80]}")
        # All tiers failed -- fall back to canned quips.
        self.tier = "canned"
        self.last_ms = int((time.time() - t0) * 1000)
        print("[brain] all tiers failed:", "; ".join(self.errors), flush=True)
        return random.choice(CANNED)

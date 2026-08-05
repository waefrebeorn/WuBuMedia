#!/usr/bin/env python3
"""
wubu_rlm.py -- self-improvement for the cohost. Recursive + reflective.

Boss, 2026-08-04: "make your talking real by using RLM and self improvement,
online research."

RESEARCH BASIS (fetched 2026-08-05, sources in out/ reports):
  * Recursive Language Models -- Alex L. Zhang et al., 2025
    https://alexzhang13.github.io/blog/2025/rlm/  (ref impl: rlm-minimal)
    Core idea: instead of one giant prompt, the model treats context as an
    environment it can query recursively, decomposing then re-calling itself.
  * Reflexion (verbal RL), Self-Refine, generative agents' reflection step,
    Mem0 (arXiv 2504.19413) for scalable long-term agent memory.

THE LATENCY PROBLEM, HONESTLY: a live cohost has ~1-2s. Self-Refine and
Reflexion cost 2-3 extra LLM calls -- 2-4 extra seconds. That is dead air on
stream, so they CANNOT sit in the reply path.

SO THIS SPLITS THE WORK IN TWO:
  FAST PATH (blocking, <1s)  -- one call, no recursion. Untouched.
  SLOW PATH (background)     -- during quiet moments a worker thread reflects
                                on recent exchanges, scores what landed, and
                                distills LESSONS that get injected into the
                                next system prompt. The cohost gets funnier
                                over a session without ever adding latency.

That is the honest way to use these techniques in real time: recursion and
critique happen off the critical path, and only their *output* (a short lesson
list) touches the live prompt.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MEM_PATH = os.path.join(ROOT, "out", "cohost_memory.json")

MAX_LESSONS = 6
MAX_EPISODES = 60


class Memory:
    """Episodic memory + distilled lessons. Survives restarts."""

    def __init__(self, path=MEM_PATH):
        self.path = path
        self.lock = threading.Lock()
        self.episodes = []      # {heard, said, ts, score}
        self.lessons = []       # short strings injected into the persona
        self.load()

    def load(self):
        try:
            with open(self.path) as f:
                d = json.load(f)
            self.episodes = d.get("episodes", [])[-MAX_EPISODES:]
            self.lessons = d.get("lessons", [])[:MAX_LESSONS]
        except Exception:
            pass

    def save(self):
        try:
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            tmp = self.path + ".tmp"
            with self.lock:
                d = {"episodes": self.episodes[-MAX_EPISODES:],
                     "lessons": self.lessons[:MAX_LESSONS],
                     "ts": time.time()}
            with open(tmp, "w") as f:
                json.dump(d, f, indent=1)
            os.replace(tmp, self.path)
        except Exception:
            pass

    def record(self, heard, said):
        with self.lock:
            self.episodes.append({"heard": heard[:300], "said": said[:300],
                                  "ts": time.time(), "score": None})
            self.episodes = self.episodes[-MAX_EPISODES:]

    def unscored(self, n=8):
        with self.lock:
            return [e for e in self.episodes if e.get("score") is None][-n:]

    def set_lessons(self, lessons):
        with self.lock:
            self.lessons = [l.strip(" -*\t") for l in lessons if l.strip()][:MAX_LESSONS]
        self.save()

    def lesson_block(self):
        """Inject as a system-prompt section. Format is concrete rules."""
        with self.lock:
            if not self.lessons:
                return ""
            body = "\n".join(f"- {l}" for l in self.lessons)
        return ("WHAT YOU'VE LEARNED THIS STREAM (follow these format facts):\n" + body)


# ---------------------------------------------------------------------------
# Reflector: background thread that critiques recent episodes and distills
# short, actionable LESSONS. Never blocks a live reply.
# ---------------------------------------------------------------------------
class Reflector(threading.Thread):
    """Background self-improvement via Reflexion-style critique.

    Fires only after `idle_needed` seconds of boss silence (so it never cuts
    off a real reply) and re-runs every `period` seconds until stopped.
    Produces at most 4 SHORT format-rule lessons, merges them into memory, and
    marks the critiqued episodes as scored so it doesn't re-dig them.
    """

    def __init__(self, brain, memory, safety, idle_needed=18.0, period=60.0):
        super().__init__(daemon=True, name="reflector")
        self.brain = brain
        self.mem = memory
        self.safety = safety
        self.idle_needed = idle_needed
        self.period = period
        self.runs = 0
        self.last_run = 0
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def touch(self):
        """Called after a real boss utterance so the reflector doesn't fire
        immediately on a fresh episode -- gives the silence window to reset."""
        self.last_run = time.time()

    def run(self):
        while not self._stop.is_set():
            time.sleep(1.0)
            # Only reflect when the boss is actually quiet -- never mid-ramble.
            # Silence is detected by the age of the most recent episode: if
            # the last thing he said was >idle_needed seconds ago, we are safe.
            eps = self.mem.unscored(8)
            if not eps:
                continue
            now = time.time()
            if now - self.last_run < self.period:
                continue
            if now - eps[-1].get("ts", 0) > self.idle_needed:
                self.last_run = now
                try:
                    self.reflect(eps)
                except Exception as e:
                    print("[reflect] failed:", str(e)[:80], flush=True)

    def reflect(self, eps):
        """One recursion: read own transcript -> critique -> distill lessons."""
        transcript = "\n".join(
            f"BOSS: {e['heard'][:120]}\nYOU: {e['said'][:120]}" for e in eps)
        prior = "\n".join(f"- {l}" for l in self.mem.lessons) or "(none yet)"
        msgs = [
            {"role": "system",
             "content": ("You are the self-critique module for a live-stream AI "
                         "cohost. You are NOT on stream; nobody sees this. Be "
                         "harsh and concrete. Every lesson must be a SHORT, "
                         "actionable format rule the NEXT reply should follow -- "
                         "not a meta-commentary about style, but a concrete "
                         " steer: 'when streamer says X, do Y'.")},
            {"role": "user",
             "content": (
                 f"Here is the cohost's recent exchange log:\n\n{transcript}\n\n"
                 f"Existing lessons:\n{prior}\n\n"
                 "Judge which lines landed as sharp, specific, streamer-tied "
                 "banter and which were generic, repetitive, bland, or missed "
                 "what the streamer meant. Then output at most 4 SHORT lines.\n"
                 "Each line is a concrete format fact starting with '- ', tied "
                 "to THIS streamer's phrasing. Output ONLY the lesson lines.\n"
                 "Examples:\n"
                 "- When streamer says 'Joel', anchor the jab to the exact action on screen right now.\n"
                 "- If streamer derails to nonsense, mirror with escalating absurdity, not generic mockery.\n"
                 "- Avoid 'X is dead' / 'survival horror' stock phrases — make it specific to the moment.")},
        ]
        out = self.brain.think(msgs, max_tokens=220, temperature=0.5, timeout=25)
        if not out:
            return
        lessons = []
        for l in out.splitlines():
            l = l.strip()
            if l.startswith("- ") and len(l) > 12:
                txt = l.lstrip("- ").strip()
                if txt and self.safety.clean(txt):
                    lessons.append(txt)
        lessons = lessons[:4]
        if not lessons:
            return
        # merge: new lessons first, dedupe by prefix to stay under MAX_LESSONS
        merged, seen = [], set()
        for l in lessons + self.mem.lessons:
            k = l.lower()[:40]
            if k in seen:
                continue
            seen.add(k)
            merged.append(l)
        self.mem.set_lessons(merged)
        for e in eps:
            e["score"] = 1
        self.mem.save()
        self.runs += 1
        print(f"[reflect #{self.runs}] {len(lessons)} new lessons:", flush=True)
        for l in lessons:
            print(f"    - {l[:100]}", flush=True)


if __name__ == "__main__":
    import sys
    sys.path.insert(0, HERE)
    import wubu_brain
    import wubu_safety
    m = Memory(path=os.path.join(ROOT, "out", "cohost_memory_test.json"))
    m.episodes = []          # fresh: persisted episodes are already scored
    m.lessons = []
    m.record("this boss fight is brutal", "Skill issue, honestly.")
    m.record("wait I got", "Got what? A loot drop or another glitch?")
    m.record("more USB lanes on my seat", "Building a throne for a data-center.")
    r = Reflector(wubu_brain.Brain(), m, wubu_safety)
    r.reflect(m.unscored())
    print("\nlesson block:\n" + m.lesson_block())

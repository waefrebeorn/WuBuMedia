# Loop Research: 4 Interconnected Loops in the AGI Cohost System

**Date:** 2026-08-05
**Status:** Complete ✅ (4 sources extracted, 3 arXiv papers, 2 industry case studies)

## Executive Summary

Four loops form the core dynamics of the WuBuDesk AGI cohost system:
1. **Recursive Self-Improvement** (Anthropic/Arxiv 2504) — agent edits its own code
2. **Recommendation Feedback** (TikTok Monolith/arXiv 2209 + Nature/Berkeley) — recs → engagement → recs
3. **Emotional State Evolution** (Evolving Agents/arXiv 2404 + OpenFeelz/GitHub) — stimulus → mood → behavior → mood
4. **Continuous Coding Loop** (SICA/arXiv 2504 + Addy Osmani blog) — task → implement → validate → commit → repeat

---

## Loop 1: Recursive Self-Improvement (RSI)

### Sources
- **Anthropic (2026):** "When AI builds itself" — https://www.anthropic.com/institute/recursive-self-improvement
- **SICA Paper (arxiv 2504.15228):** "A Self-Improving Coding Agent" — https://arxiv.org/html/2504.15228v1
- **Addy Osmani Blog:** "Self-Improving Coding Agents" — https://addyosmani.com/blog/self-improving-agents/

### Key Findings

#### The 5-Stage Loop (SICA)
1. **Capture traces** — Run the agent to collect execution data
2. **Run improvement skill** — A meta-agent analyzes traces and identifies improvements
3. **Edit codebase** — Apply code changes (tools, prompts, strategies)
4. **Validate** — Run eval suite, measure performance improvement
5. **Iterate** — Repeat until convergence or resource budget exhausted

#### Critical Design Decisions
- **Stateless per iteration:** Each iteration starts with a fresh agent process, avoiding context overflow
- **AGENTS.md pattern:** Persistent context file carrying learnings between iterations
- **Atomic tasks:** Each improvement is small enough for one session with clear pass/fail criteria
- **Compound loops:** Analysis → Planning → Execution phases chained together

#### Anthropic Evidence
- 80% of merged code at Anthropic written by Claude (May 2026)
- SWE-bench performance: models went from single digits to saturation in 2 years
- Task length doubling every 4 months (4 min → 12 hours → 16+ hours autonomous)
- **Key insight:** "The rate at which AI models improve is accelerating"

### Implementation for WuBuDesk

**Current state:** `wubu_self.c` runs periodic checks but doesn't actually improve code.

**Research-driven improvements:**
1. Replace the 6 generic checks with the SICA 5-stage loop
2. Add `AGENTS.md` equivalent — a `knowledge/self_learn.md` file that accumulates findings
3. Make each improvement iteration atomic (bounded scope, clear criteria)
4. Add validation via the existing test suite (`build_c11.sh test`)

---

## Loop 2: Recommendation Feedback Loop

### Sources
- **TikTok Monolith (arXiv 2209.07663):** "Real Time Recommendation System with Collisionless Embedding Table"
- **Berkeley Center for Human-Compatible AI (Medium):** "When You Hear Filter Bubble, Echo Chamber, or Rabbit Hole — Think Feedback Loop"
- **Nature/Springer:** "Echo chamber effects on short video platforms" (s41598-023-83370-1)

### Key Findings

#### The 3-Variable Feedback Loop
The core loop has three variables that feed each other:
- **What is engaged with** ← user action (watch, like, share, complete)
- **What is shown** ← what the algorithm presents
- **What is thought** ← user's internal state/opinion

Only "what is shown" is algorithmically controlled. The loop forms when:
`engagement → changes algorithm → changes what is shown → changes what is engaged with`

#### Monolith Architecture
- **Collisionless embedding tables** via Cuckoo Hashing (O(1) insert/lookup/delete)
- **Two-stage training:** Batch training → Online training with real-time feedback
- **Concept drift handling:** Online params sync to serving params every N seconds
- **Three filtering methods:** Frequency filter, probabilistic filter, expire filter

#### Echo Chamber Mechanisms
- **High engagement** → algorithm interprets as "user wants more like this"
- **Narrowing diversity** → recommendations become increasingly homogeneous
- **Emotional resonance** drives the loop faster (stronger emotions = stronger engagement signals)

### Implementation for WuBuDesk

**Current state:** `wubu_recs.c` implements a 3-stage pipeline (candidate → ranking → re-ranking) with Algo 101 scoring.

**Research-driven improvements:**
1. Add **concept drift handling** — recency weighting in scoring (Monolith)
2. Add **diversity injection** to break echo chambers (Berkeley research)
3. Add **rumination-like engagement** — if user engages with same topic N times, create a "belief cluster" in RLM
4. Add **emotional valence** as an engagement signal (links to emotion loop)

**Cuckoo Hashing for recs:** Replace the linear scan in candidate retrieval with a hash table for O(1) tag→video lookup.

---

## Loop 3: Emotional State Evolution

### Sources
- **Evolving Agents (arXiv 2404.02718):** "Interactive Simulation of Dynamic and Diverse Human Personalities"
- **OpenFeelz (GitHub):** trianglegrrl/openfeelz — PAD + Ekman + OCEAN emotional model

### Key Findings

#### The Personality-Emotion-Behavior Feedback Loop
```
External stimulus → Emotion system → Behavior system → Personality Change → (back to Emotion)
```

**Emotion system** (3 modules):
- **Cognition** — process external info, generate interpretations
- **Emotion** — valence + arousal from cognition
- **Character Growth** — traits evolve based on emotional experiences

**Behavior system** (2 modules):
- **Planning** — decide next action based on emotion + personality
- **Action** — execute, generating new experiences

#### OpenFeelz Decay Model
- **Exponential decay:** Emotions fade toward baseline over time
- **Personality modulation:** OCEAN traits influence baseline and decay rates
  - High neuroticism → negative emotions linger (0.84-0.88x decay)
  - High extraversion → sadness fades faster (1.16x)
- **Rumination engine:** Intense emotions (>threshold) persist across interactions
- **Emotional short-term memory:** Agent remembers you've been frustrated for last 3 messages

#### Key Numbers (OpenFeelz defaults)
- Joy: half-life ~1 hour (fast decay)
- Sadness: half-life ~15 hours (slow decay)
- Anger: half-life ~8 hours

### Implementation for WuBuDesk

**Current state:** `wubu_emotion.c` has 7 moods (sad, neutral, happy, excited, very_happy, ecstatic, angry) with prosodic feature extraction.

**Research-driven improvements:**
1. Add **OCEAN personality traits** (5 dimensions) — store in cohost struct
2. Add **exponential decay** — mood fades toward OCEAN-influenced baseline over time
3. Add **rumination engine** — persistent emotional state across interactions
4. Add **personality-modulated decay rates** — different traits affect decay speed
5. Link **engagement events** (from recs loop) → emotional state changes

---

## Loop 4: Continuous Coding Loop

### Source
- **SICA Paper (arXiv 2504.15228)** and **Addy Osmani's blog**

### Key Findings

#### The Ralph Wiggum Technique
1. Pick next task from a todo list
2. Implement the task (fresh agent spawn)
3. Validate (tests, type checks)
4. Commit if checks pass
5. Update task status + log learnings
6. Reset agent context, repeat

#### Context Management
- **AGENTS.md:** Persistent knowledge file, pruned to most relevant tips
- **Context injection:** Only include task-relevant sections
- **Summarization:** Agent summarizes old progress logs, truncates
- **Task isolation:** Each iteration = fresh process, bounded context

#### Compound Loops
Advanced workflows chain multiple phases:
- **Analysis loop:** Read reports, identify what to build
- **Planning loop:** Generate PRD → tasks JSON
- **Execution loop:** Implement tasks one by one

### Implementation for WuBuDesk

**Current state:** `wubu_self.c` has a basic scheduler that runs checks every interval.

**Research-driven improvements:**
1. Implement the **Ralph Wiggum loop** in `wubu_self.c`
2. Replace generic checks with **task list → implement → validate → commit → learn**
3. Add `knowledge/self_learn.md` as the AGENTS.md equivalent
4. Add **context summarization** — compress old log entries
5. Add **compound loop structure** — analysis → planning → execution phases

---

## Cross-Loop Interactions

```
┌─────────────────────────┐
│  Continuous Coding Loop │
│  (SICA: self-improve)   │
└──────────┬──────────────┘
           │ code changes
           ▼
┌─────────────────────────┐    ┌──────────────────────┐
│  Recommendation Loop     │──→ │ Emotional Evolution   │
│  (Monolith: what's shown)    │  (OpenFeelz: mood)   │
└──────────┬──────────────┘    └──────────┬───────────┘
           │ engagement                     │ behavior change
           ▼                                ▼
┌─────────────────────────┐    ┌──────────────────────┐
│  Self-Improvement Loop  │◄───│ Behavior Feedback     │
│  (Anthropic: code edits)      │  (new data to learn) │
└─────────────────────────┘    └──────────────────────┘
```

### Key Cross-Loop Integrations

1. **Recs → Emotion:** High recs engagement → positive emotional valence → higher energy state
2. **Emotion → Recs:** Mood state modulates recommendation weights (e.g., "sad" → more comfort content)
3. **Coding → All:** Code changes to any module affect all loop dynamics
4. **All → Coding:** Interaction logs feed the self-improvement loop's analysis phase

---

## Implementation Plan (C11)

### Phase 1: Enhance RLM with Personality (Loop 3)
- Add `RLMPersonality` struct (5 OCEAN dimensions) to `wubu_rlm.h`
- Add exponential decay to mood (OpenFeelz half-life model)
- Add rumination: persistent mood state across sessions

### Phase 2: Enhance Recs with Concept Drift (Loop 2)
- Add recency weighting to Algo 101 scoring
- Add diversity injection to prevent echo chambers
- Add emotional valence as engagement signal

### Phase 3: Enhance Self-Improvement (Loops 1 + 4)
- Replace `wubu_self_check` with the SICA 5-stage loop
- Add `knowledge/self_learn.md` AGENTS.md equivalent
- Add compound loop: analysis → planning → execution
- Add **15-minute timer** with container isolation and git rollback
- Add `wubu_sica.c` module implementing the full loop

### Timer Architecture (15-Minute Interval)

The SICA loop runs on a 15-minute timer with these properties:

1. **Scheduler thread**: `wubu_sica_run_scheduler()` uses `pthread_create` (or
   a background thread) with `sleep(interval_seconds)` where default = 900s.
2. **Container isolation**: Each cycle runs in a fresh `popen` process —
   no state leakage between iterations (the "Ralph Wiggum technique").
3. **Git rollback**: Pre-cycle `git stash` snapshot; post-validation commit or
   `git reset --hard` rollback if tests regress.
4. **Research wiring**: Cycle 2+ scans `knowledge/` for new research notes
   that may inform improvement decisions.

**Implementation**: `src/wubu_sica.c` — 5-stage loop with container isolation,
git helpers (commit/rollback), and 15-minute timer. All functions are
opaque-struct + C11 only. Tests in `test_sica.c` (9/9 pass).

See: `knowledge/SICA_RESEARCH.md` for full implementation details.

### Phase 4: Cross-Loop Integration
- Wire recs engagement → emotion state
- Wire emotion → recs weights
- Wire interaction logs → self-improvement analysis

---

## Verification Criteria

Each loop improvement must pass:
- ✅ Compiles with `-Wall -Wextra -std=c11` (zero warnings)
- ✅ Passes existing test suite (no regressions)
- ✅ New test demonstrates the loop behavior (feedback, decay, iteration)
- ✅ C11 opaque struct + single responsibility maintained
- ✅ No external dependencies beyond SQLite + ws2_32

**Research sources:** 6 papers/articles, 2 GitHub repos (Monolith archived, OpenFeelz active)
**License compliance:** All sources are academic/opensource, C11 implementations are original code

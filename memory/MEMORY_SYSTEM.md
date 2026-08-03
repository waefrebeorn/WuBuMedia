# WuBuDesk — Memory & Persona Recursive System (cursive learning cycles)

> How the cohost learns, remembers, and becomes itself — on Windows, feeding the
> WuBu AGI (WuBuOS). License: SPDX-License-Identifier: WaefreBeorn-UMV3.

## Principle
The persona is *formative* — it builds as it goes. Not a static character sheet,
but a recursive loop: **perceive → store → reflect → revise → act**. Each cycle
tightens the cohost's fit to the boss, the stream, and the real tech it advertises.

## Layers (Seven Degrees of Kevin Bacon, applied to memory)

1. **Episodic (stream events).** What happened on stream: games played, chat mood,
   topics, the boss's live statements. Source: OBS state + chat + STT transcripts.
2. **Semantic (facts).** Stable truths: boss lore (emangamer/mangamer, Corn Man,
   MCN manager, internet-since-2009), repo facts (wubuwizard is C11 SSM+MoE,
   bonzi is the companion core), hard rules (no healthcare/UHC talk, no doxxing).
3. **Procedural (how-to).** Skills: launch OBS, drive cua, run STT, build toolchain,
   quarantine PPI. Stored as reusable skills (the agent's skill system).
4. **Affective (mood/empathy).** Mirrors wubu_bonzi.c: valence/arousal state,
   empathy response, mood→prosody. The cohost warms if the room is down.
5. **Persona (self-model).** The Bonzi self-model: who the cohost is *on stream* —
   the JARVIS-like, Hollywood-shaped rep of the real AGI. Distinct from the agent's
   internal operating memory.
6. **Attested-mutation log.** Every self-change is recorded (mirrors WuBuOS
   `dgm_archive.json` + the umbrella license's attested-mutation clause). The
   cohost never silently rewrites its own rules — changes are logged + reviewable.
7. **Bridge to WuBuOS.** The Windows-side memory serializes into the format the
   Linux AGI consumes, so the persona is one representation of a larger mind.

## Recursive learning cycle (cursive)
```
observe (screen/chat/stt/github)
   -> encode (tag: episodic/semantic/procedural/affective)
   -> store (memory store, dedupe, expire stale)
   -> reflect (devil's-advocate: is this claim verified? conflicts?)
   -> revise (update persona fit, mood model, skills)
   -> act (speak via OBS face, drive cua, post blurb)
   -> observe(aftermath) -> loop
```
Each loop is bounded by the BOUNDARIES (never starve GPU, never fake, ToS-safe).

## Storage (Windows-side)
- `obs/memory_store.json` — episodic + semantic (encrypted-at-rest optional later).
- `obs/persona_state.json` — affective + persona self-model (the bonzi mirror).
- `obs/skills/` — procedural (the agent's skill system, persistent).
- `obs/dgm_archive.json` — attested-mutation log (every self-edit).
- All under the umbrella license; nothing PII (quarantined/deleted already).

## Verification (Devil's-Advocate)
- A memory claim is only "semantic" if backed by (a) boss statement, (b) a repo
  commit, or (c) a verified tool result. Otherwise it's "unverified" and flagged.
- Persona acts are checked against hard rules before they reach the stream.

## Why this matters
The boss: *"creating a formative persona that builds as it goes… cursive learning
cycles… integrate."* This doc is the spine. The cohost is one node of a bigger AGI;
its memory is honest, attested, and feeds the whole.

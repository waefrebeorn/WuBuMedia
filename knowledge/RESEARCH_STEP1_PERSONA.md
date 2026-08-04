SPDX-License-Identifier: WaefreBeorn-UMV3

# Step 1 — Persona & Cohost Craft (research)

## Overlay / popup-trigger logic (OBS browser source)
- OBS browser sources are **transparent by default** — pure HTML/CSS overlays
  need no green screen. (Confirmed earlier this session.)
- **Popup trigger patterns** (StreamElements / OBS tutorials 2025-2026):
  - *Reactive image*: source appears when a signal fires (chat mention, event),
    disappears after a timeout. Implement via JS polling `face_state.json`:
    show when `visible=true`, hide (opacity 0) after N seconds of silence.
  - *Disappearing overlay animation*: CSS opacity/transform transitions on a
    timer — no extra software needed.
  - *PNG VTuber collab*: friend appears when they talk, vanishes after — same
    signal-driven model we use for WuBuDesk.
- **Our impl:** `face/index.html` polls `face_state.json`; `wubu_obs.speak()`
  sets mood/text/mode. Add `visible` + `ttl` fields to face_state for popup.

## Talking-to-streamer patterns (cohost, not chatbot)
- Social Stream Ninja AI co-host guide; ai_licia / Questie: fill gaps so the
  streamer has "something to talk to." Best when REACTIVE (responds to chat/
  events) not just proactive.
- Tone: short, punchy, boss/chapter, honest receipts. Calibrated sass, not spam.
- Multi-turn session memory; know when to stay quiet (don't talk over boss).

## Feedback loop (the Reflexion spine — core of the cohost)
- Reflexion (promptingguide.ai): Act -> Evaluate (env/signal) -> Reflect
  (verbal lesson) -> Memorize -> Improve next episode. "Learn once, ace after."
- Self-improving agents: Execution -> Evaluation -> Reflection -> Memory ->
  Optimization. Human-in-loop keeps goal alignment (datagrid tip #7).
- Our impl: `memory/reflections.json` (L001-L004) + cron `e2ef49e096c3`
  (weekly self-review) + PERSONA_ITERATION_PLAN.md 7-step/100-step.

## Requests for back-room agents (from this step)
- Colonels: expose an OBS-layout lock so the cohost can never disrupt the boss's
  live layout (already in BOUNDARIES.md; enforce via API guard).
- Engine: none directly; posture is cohost-side.

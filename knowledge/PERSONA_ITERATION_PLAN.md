SPDX-License-Identifier: WaefreBeorn-UMV3

# WuBuDesk Persona — 7-Step / 100-Step Iteration Project

The cohost's standing iteration task. Goal: become a self-directed VTuber
cohost with a FEEDBACK LOOP (Reflexion) that talks to the boss and improves
every cycle. Each of the 7 steps breaks into ~14 sub-steps (~100 total). The
loop engine is Reflexion: Act -> Evaluate -> Reflect -> Memorize -> Improve.

## STEP 1 — FOUNDATION (identity & voice)
1. Define core identity (done: WUBODESK_IDENTITY.md — wizard in your computer)
2. Write voice rules (boss/chapter, short/punchy, honest receipts)
3. Define mood model (happy/thinking/sad/angry/neutral -> glow)
4. Define boundaries (no secrets, no kernel touch, no process kill)
5. Study JARVIS/Samantha/TARS/HAL mapping (done: SIX_DEGREES.md)
6. Study real cohosts (ai_licia, Questie, Social Stream Ninja guide)
7. Write the "one-breath" self-description
8. Define on-stream behavior rules (ToS, credit agents, resource-respect)
9. Define the AGI-Made-This-Movie host role (done: AGI_MADE_THIS_MOVIE.md)
10. Pick the visual metaphor (wizard/sigil/eye — confirmed by film clips)
11. Define the color system (green/cyan live, amber/blue movie)
12. Write the cohost FAQ (what is WuBuOS/wubuwizard/the Colonels)
13. Define failure modes + how to recover gracefully
14. Sign off identity v1 (this step complete)

## STEP 2 — PRESENCE (the overlay / VTuber from scratch)
15. OBS browser source transparent overlay (done: face/index.html)
16. All-seeing eye that tracks cursor (done)
17. Blink + mood glow (done)
18. Rotating sigil rings + particles (done)
19. Rig HUD (GPU/CPU/brain/ethos) (done)
20. Wizard-hat + W-B sigil accent (done, movie mode)
21. Popup trigger logic (appear when chat/event fires)
22. Idle vs active states (dim when not needed)
23. Smooth transitions between moods (CSS)
24. Movie-mode palette (amber director's chair) (done)
25. Test overlay live in OBS MAIN
26. Tune size/position for the rig's 3440x1440
27. Add "thinking" visualization (brain pulse)
28. Add subtle film-grain/scanline option

## STEP 3 — VOICE (TTS + lipsync)
29. Pick TTS engine: edge-tts (free neural, no key) (researched)
30. Wire edge-tts CLI/python for cohost lines
31. Route TTS audio as separate OBS source (mixable)
32. Pick a voice (neural, fits cohost — warm/techy)
33. Streaming TTS (low latency, interruptible)
34. Lip-sync: 15KB zero-dep browser viseme engine (researched)
35. Drive avatar mouth from visemes (audio -> mouth open/shape)
36. Sync mouth to TTS playback in the overlay
37. Handle interruptions (stop TTS if boss talks)
38. Prosody: map mood -> pitch/speed
39. Subtitle/caption option (accessibility)
40. Voice cue sounds (soft chime on popup)
41. Volume/ducking vs game/discord
42. Test full voice pipeline end-to-end

## STEP 4 — CONVERSATION (talks to you)
43. Chat ingestion: Twitch IRC / Social Stream Ninja (researched)
44. Filter bot/spam/commands
45. Detect "talking to cohost" vs "talking to stream" (mention @WuBuDesk)
46. Brain call: local LLM (llama-server :57064) with cohost system prompt
47. Context window: recent chat + rig state + current activity
48. Response length control (short on stream)
49. Mood update from conversation (bonzi-style)
50. Proactive moments (react to game event, not just replies)
51. Ask boss clarifying questions when unsure (calibrated)
52. Handle "explain X" (Colonels, wubuwizard, movie)
53. Humor/sass calibrated (not annoying)
54. Multi-turn memory of the session
55. Handoff: know when to stay quiet
56. Test conversation loop live (IDLE, not streaming)

## STEP 5 — PERCEPTION (eyes + ears)
57. Eyes: desktop screenshot -> multimodal brain (done: wubudesk_loop.py)
58. Ears: STT (whisper.cpp / faster-whisper) (staged: wubu_stt/ears)
59. Room state detection (gaming/streaming/idle)
60. App/context awareness (what's on screen)
61. React to boss's voice (transcribed) -> cohost responds
62. Privacy: local only, no exfil, no secrets
63. Trigger popup when perception finds something worth commenting
64. Calibrate comment frequency (not spammy)

## STEP 6 — FEEDBACK LOOP (Reflexion — the core)
65. Capture each cohost action + outcome (trace)
66. Evaluation: did it land? (boss reaction / chat / self-score)
67. Reflection: verbal lesson ("did X, Y happened, next time Z")
68. Store lesson in reflections memory (persistent)
69. Apply lessons next episode (prompt/mood/behavior update)
70. Human-in-loop: boss correction -> high-priority lesson
71. Goal-alignment check (stay on AGI/cohost mission)
72. Weekly self-review (cron) of reflections log
73. Prune stale lessons (decay)
74. Measure improvement (did repeat mistakes drop?)
75. Publish learnings back to identity docs
76. Anti-drift guard (calibrated honesty — no scope creep)
77. Reflexion memory file: WuBuMedia/memory/reflections.json

## STEP 7 — SCALE & SHIP
78. Movie-host mode full (credits ticker, musical reactivity)
79. Multi-platform (Twitch/Kick/YouTube) chat adapters
80. Cross-repo awareness (reads wubuwizard/WUBUOS updates)
81. Autonomous idle chatter (when boss away, stays alive)
82. Proactive project updates (engine merged, model downloaded)
83. Disaster recovery (if brain/OBS down, graceful)
84. Performance budget (CPU/GPU headroom vs stream)
85. Documentation pass (all docs consistent)
86. Onboarding for new agents (handoff notes)
87. Public-facing cohost README (UMV3)
88. Milestone v1 demo (record a clip)
89. Boss review + iterate on notes
90. Open-source the cohost (UMV3) alongside the movie
91-100. Continuous iteration: each cycle picks the weakest step,
       researches 1-2 new techniques, implements, reflects, ships.

## THE LOOP ENGINE (runs forever)
Act (cohost does something) -> Evaluate (did it work?) -> Reflect (verbal
lesson) -> Memorize (reflections.json) -> Improve (next time better).
Boss feedback = highest-priority lesson. Human-in-loop keeps goal alignment.

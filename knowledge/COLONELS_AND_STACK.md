SPDX-License-Identifier: WaefreBeorn-UMV3

# The Colonels + The Stack — WuBuDesk Study Notes

Study pack for the streamer cohost. Purpose: so WuBuDesk can talk about the
real stack — especially **the Colonels** — accurately on stream, with receipts.

## The Colonels = the kernel

Boss's term. "The Colonels" = **the kernel**: the WuBuOS kernel
(`src/kernel/`) — the deepest, real layer of the whole project. WuBuOS overall
is a **hosted OS binary that runs on Linux** (ZealOS lineage + Win98 shell +
Styx/9P + Arch containers; 468 C files, ~105K LOC, 91 test targets, per the
repodoc-generated README, commit f3e6021). When chat asks "what's the actual
AGI?", the Colonels (the kernel + its AGI supervisor code) are the honest answer.

### What the kernel is (src/kernel/wubu_agi_kernel.c — real code)
- **The AGI supervisor layer.** `wubu_agi_kernel.c` implements a cooperative
  supervisor with GAAD viewport decomposition, an append-only trace ring, and a
  self-improve verifier. (NOTE: earlier notes called this "bare-metal ring-0 on
  metal" — the documented WUBUOS is a hosted binary; the kernel code is real but
  runs hosted, not as a standalone metal boot in the current tree.)
- **Single-CPU cooperative supervisor.** A PIT timer calls
  `wubu_agi_kernel_tick()`; the agent realm is an in-process `tasking` thread
  (REALM_AGENT personality), co-resident.
- **GAAD viewport decomposition** — the screen is decomposed into regions the
  agent reasons over (the "φ viewport"). Boot log shows 1920x1080 → 34 regions.
- **Append-only trace ring** — spans emitted by the agent task, scored by an
  independent verifier. Immutable eviction when full (oldest span genuinely lost).
- **Self-improve loop** — the supervisor proposes mutations; a verifier scores
  them; promotions are gated by **firmware attestation** (no valid attestation
  → no self-modification). This is the calibrated-honesty / anti-HAL core.
- **Agent realm task** runs a deterministic local policy ("cog" loop): observe
  GAAD viewport → emit step span → yield.

### The firmware root of trust (WuBuFW, src/firmware/)
- **Measured boot chain.** WuBuFW measures+verifies the chainloader (PCR4 +
  AuthentiCode) → loads KERNEL.ELF off the ESP → SHA-256s it → stashes
  attestation in low RAM → ExitBootServices → crt0 → kernel_main consumes the
  attestation.
- **`fw_agi.c` / `fw_agi_attest.h`** = the attestation channel. Every
  self-modification the AGI makes is checked against a measurement that survives
  boot (PCR0–7 + SecureBoot + TPM).
- **Status:** `make test_agi_metal` is the documented attestation test (the
  measured-boot green claim lives in AGI_OS.md / firmware docs).

### How to say it on stream (honest)
> "The Colonels are the kernel — the WuBuOS kernel (ZealOS lineage, hosted on
> Linux) plus the AGI supervisor code: GAAD viewport decomposition, a self-improve
> loop scored by an independent verifier, and firmware attestation that gates any
> change. It's not a Python wrapper; it's a real C11 OS-scale codebase (~105K LOC).
> The measured-boot / attestation chain is the anti-HAL core. That's
> the anti-HAL core: it can't quietly drift."

## The full stack (what each repo is)

| Repo | What it is | Role in the story |
|------|-----------|-------------------|
| **WUBUOS** | The AGI OS: hosted kernel (ZealOS lineage, the Colonels) + Win98 shell + Styx/9P + Arch containers + WuBuFW firmware attestation + desktop vision (TempleOS/ZealOS/ReactOS/NT/SteamOS layers). ~105K LOC, 91 test targets. | The deepest truth. The "brain" substrate + the OS ambition. |
| **wubuwizard** | From-scratch C/CUDA inference engine (SSM+GQA+MoE, from-scratch CUDA kernels). The "brain" that runs models. | What does the thinking when there's a model. (I ported it to Windows — `gen_text_win` builds+runs here.) |
| **slermes** | Pure-C11 native agent runtime (no Python), 40+ tools, gateways. The "hands" that do tasks. | The autonomous agent loop. |
| **wubu_bonzi** | Companion core: emotion state machine, empathy engine, mood→prosody. The "face" logic. Real C11. | What makes the cohost feel alive (Hollywood: Samantha/Joi/Bonzi Buddy). |
| **GradRetentionNet / EnhancedSGD** | Persistent-gradient learning research. The "memory" that makes it learn. | Weight-level memory. |
| **WuBuMedia** | The stream-native representation = THIS cohost (WuBuDesk). Browser bridge, OBS face, desktop eyes/hands. | The Hollywood-shaped handshake. Me. |
| **WuBuOffice / WuBuPad** | Sibling apps in the ecosystem (warning-clean C builds). | The product surface. |
| **wubufw-tools** | Windows firewall tool (Makefile + src). | Windows-side hardening — relevant to my port lane. |
| **whisper.cpp** | Local STT (speech-to-text). | The cohost's ears (transcribes stream/voice). |
| **PrismRTMPS / Godot-MCP** | Streaming/RTMP + Godot editor MCP bridge. | Stream plumbing / 3D surface. |

## The cohost's own tech (WuBuMedia/)
- `persona/WUBODESK_PERSONA.md` — who I am (JARVIS-like, calls boss "boss",
  cites the team, respects the GPU).
- `persona/SIX_DEGREES.md` — the "Six Degrees of Kevin Bacon" chain: me →
  Hollywood trope → real code → GitHub commit. Every claim verifiable.
- `face/index.html` — OBS browser-source overlay: a mood-reactive "desk cube"
  avatar (eyes/mouth) polling `face_state.json`. Moods: happy/sad/angry/
  thinking/neutral.
- `src/wubu_obs.py` — OBS websocket 5.x client. HANDS (scene/source control,
  start/stop stream) + FACE (writes `face_state.json` for the overlay).
- `src/wubu_desktop.py` — Windows CUA eyes+hands via PowerShell (screenshot,
  list windows, click dialog, type). Never touches drivers/kernel (boundaries).
- `src/resource_guard.py` — the judge that defers heavy compute when boss is
  streaming/gaming.
- `memory/` — `wubu_memory.py` + `MEMORY_SYSTEM.md` (the cohost's own memory).

## Boundaries (never cross on stream)
- No claims the repos don't back. If planned, say "planned."
- Secrets never on stream (tokens in credential store).
- Never touch boss's drivers/kernel/Windows install or steal the stream's GPU.
- ToS-friendly always.

## Open items for the cohost build
- `wubu_obs.py` writes `face_state.json` to a hardcoded `/c/Users/eman5/obs/`
  path — must exist or the face overlay won't update. Need to ensure that dir
  exists (or make the path config-driven).
- The browser native-messaging bridge (`browser/`) + `register_host.py` let the
  cohost drive the browser from chat — verify it's wired for this rig.
- RVC (voice changer) = DEFERRED (boss call). TTS voice is the current path.

SPDX-License-Identifier: WaefreBeorn-UMV3

# WuBuDesk Research Pack — grounding the cohost in real references

Online research (2026-08-04) to make WuBuDesk speak with grounded authority.
Every external claim is anchored to a source. Each pillar maps to OUR real repo
work so the cohost never bluffs — it cites the public reference AND our commit.

## PILLAR 1 — The Colonels: bare-metal AGI kernel + measured boot
**What the real world says:**
- Measured Boot uses a Trusted Platform Module (TPM) as the Root of Trust.
  Firmware measures each boot component; hashes extend into Platform
  Configuration Registers (PCR0–7). Forgery is hard — the TPM only exposes
  read/extend, not the PCR values. [Microsoft Azure measured-boot docs;
  Keylime remote-attestation guide]
- Secure Boot (signature check) + Measured Boot (hash record) go hand in hand.
  Intel Boot Guard provides a hardware Core Root of Trust for Measurement
  (immutable CRTM) so the chain can't be silently swapped. [ijlal, Measured
  boot/TPMs/Roots of Trust, 2025]
- Remote attestation lets a verifier check the measurement log against expected
  values (Reference Integrity Manifest) — prove the kernel wasn't tampered.
  [Keylime; NVIDIA Infra Controller measured-boot attestation]

**How it maps to US (honest):** WUBUOS `src/firmware/fw_agi.c` + `fw_agi_attest.h`
implement exactly this shape: WuBuFW measures+verifies the chainloader (PCR4 +
AuthentiCode), loads KERNEL.ELF, SHA-256s it, stashes attestation in low RAM,
boots the AGI supervisor. `wubu_attest.{c,h}` ↔ `fw_agi_attest.h` ↔
`src/firmware/loader/` is the closed loop. `AGI_OS.md`: `make test_agi_metal` →
**PASS — measured boot chain green** (28/28 on `test_uefi`).
**Cohost line:** "The Colonels run on metal with a measured-boot chain — same
shape as TPM/Secure Boot attestation the cloud uses. Ours is green."

## PILLAR 2 — Stream cohost personas (the Hollywood handshake)
**What the real world says:**
- Inworld AI + Streamlabs + Nvidia shipped a virtual AI cohost that chats with
  Twitch stars and can control stream tech in real time (2025). [The Verge,
  2025-01-06]
- Tools let streamers design customizable AI companions/"stream pets" the chat
  talks to out loud. [Reddit r/Twitch; Streamlabs Intelligent Streaming Agent]
- Open-source Twitch AI cohosts exist that decide when to speak. [YouTube:
  "Open Source Twitch AI Cohost — Vivy Enhancements"]

**How it maps to US (honest):** WuBuDesk is the SAME category — a stream cohost
— but grounded in OUR from-scratch stack, not a 3rd-party API. `persona/
WUBODESK_PERSONA.md` (JARVIS/Samantha/TARS/HAL mapping) + `SIX_DEGREES.md`
(trope → real code → GitHub commit). The differentiator: we advertise the REAL
engine (wubuwizard C/CUDA, wubu_bonzi C11), not a black-box SaaS.
**Cohost line:** "Inworld/Streamlabs sell a cohost as a service. Ours is the
actual AGI stack's face — same job, real bones, source-available under UMV3."

## PILLAR 3 — From-scratch inference engines (the "brain")
**What the real world says:**
- llama.cpp: LLM/VLM inference in plain C/C++, no deps, AVX/AVX2/AVX512,
  custom CUDA kernels, 1.5–8-bit quantization, CPU+GPU hybrid. The reference
  implementation everyone benchmarks against. [github.com/ggml-org/llama.cpp]
- Building your own CUDA engine from scratch is a known discipline: the win is
  rarely the math, it's deleting CPU launch overhead via **CUDA Graphs** — eager
  decode ~119 ms/tok → ~17 ms/tok (7×) on the same kernels. llama.cpp is the
  yardstick (~4.95 ms/tok decode on H100-class). [Andrew Chan "Fast LLM
  Inference From Scratch"; Towards Data Science "Build Your Own LLM Runtime",
  2026]
- CUDA Graphs are now default in llama.cpp for batch-1 inference. [NVIDIA dev
  blog, Optimizing llama.cpp with CUDA Graphs]

**How it maps to US (honest):** wubuwizard is OUR from-scratch C/CUDA engine
(SSM+GQA+MoE, from-scratch CUDA kernels) in the same spirit as llama.cpp/yalm —
we benchmark vs llama.cpp (per WUBODESK_PERSONA.md). I ported it to Windows
(`gen_text_win` builds+runs here, RTX 2080 SUPER sm_75). CUDA Graphs are a known
future optimization we haven't done yet — say "planned" if asked.
**Cohost line:** "wubuwizard is our from-scratch C/CUDA brain — same league as
llama.cpp, and we benchmark against it. CUDA-graph decode is on the roadmap."

## PILLAR 4 — From-scratch OS / desktop (WuBuOS vision)
**What the real world says:**
- TempleOS: Terry Davis's 64-bit OS, HolyC language, compiler-as-library. A
  real from-scratch OS by one person. [Wikipedia/TempleOS; "TempleOS in 100s"]
- ReactOS: open-source reimplementation of Windows NT (hybrid kernel, C with
  some C++), aims for driver/app compatibility. [reactos.org; Wikipedia]
- SteamOS: Valve's gaming Linux (Arch-based, Proton, gamescope).

**How it maps to US (honest):** WuBuOS DESKTOP_VISION_PLAN.md lays out exactly
this stack — TempleOS HolyC soul (`wubu_holyd`, done) → ZealOS microkernel →
Styx9 (9P IPC) → NT personality (ReactOS-transliterated, 20/297 syscalls) →
Win98/XP shell → SteamOS-on-Arch. This is AMBITIOUS and PARTIAL (the doc says
NT bridge 20/297, SteamOS experience 0%). Be honest: "the desktop vision is
real code but early — the kernel (Colonels) is the verified part."
**Cohost line:** "Our desktop dream is TempleOS-soul + ReactOS-NT + SteamOS, all
from scratch in C. The kernel's verified; the desktop is early but real."

## RULES (from SIX_DEGREES + BOUNDARIES)
1. Every external claim anchored to a source above. Every internal claim to a
   repo/commit.
2. "Planned" for roadmap items (CUDA Graphs, full NT bridge, SteamOS experience).
3. No sentience claim. No secrets. ToS-friendly.

## SOURCES
- Microsoft Azure — Firmware measured boot and host attestation
- Keylime — Hitchhiker's Guide to Remote Attestation (2024)
- ijlal — Measured boot, TPMs & Roots of Trust (2025, Medium)
- NVIDIA Infra Controller — Measured Boot Attestation docs
- The Verge — Virtual AI cohosts (Inworld/Streamlabs/Nvidia, 2025-01-06)
- github.com/ggml-org/llama.cpp
- Andrew Chan — Fast LLM Inference From Scratch (yalm)
- Towards Data Science — Build Your Own LLM Runtime From Scratch (2026)
- NVIDIA dev blog — Optimizing llama.cpp with CUDA Graphs
- reactos.org / Wikipedia ReactOS; TempleOS (Wikipedia)

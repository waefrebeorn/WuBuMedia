# WuBuMedia

> The **stream-native representation** of the WuBu AGI stack.

WuBuMedia is the public face layer for the from-scratch C/CUDA AGI the team is
building (see `waefrebeorn/wubuwizard`, `waefrebeorn/slermes`,
`waefrebeorn/GradRetentionNet`). Where the Linux agents do the heavy lifting,
WuBuMedia is the cohost that lives on the Windows stream rig — the friendly,
Hollywood-shaped handshake that makes the deeper tech legible and famous.

As the boss puts it: *"Hollywood has its own representation of a technology, even
if the tech is more advanced somewhere else. We have multiple representations to
help it advance. You're the one on the stream representing and advertising our
actual much greater technologies."*

## What's in here

| Path | What |
|------|------|
| `persona/` | WuBuDesk cohost persona spec + the "Six Degrees of Kevin Bacon" research mapping the cohost -> Hollywood tropes -> real GitHub tech |
| `face/` | The cohost "face" overlay (browser source for OBS) — the rendered representation on stream |
| `feed/` | `github_feed.py` — turns the agents' real GitHub commits into stream-ready blurbs |
| `src/` | Small helper tools (OBS control, resource guard) the cohost runs |

## The persona in one breath

WuBuDesk is the JARVIS-like cohost that actually runs the rig: it manages the
cluster, watches the GPU so it never steals the stream's encode, and talks to
chat. Under the hood the companion core (`wubu_bonzi.c`) is **real C11** — an
emotion state machine, empathy engine, and mood→prosody mapper the agents
shipped. The full inference engine (`wubuwizard`) is a from-scratch SSM+GQA+MoE
substrate with CUDA kernels. We advertise the *real* thing, backed by commits.

## License — IMPORTANT

Everything here is published under the **[WaefreBeorn Umbrella License v3.0](https://github.com/waefrebeorn/waefrebeorn-umbrella-license)**
(`SPDX-License-Identifier: WaefreBeorn-UMV3`).

- Source-available (not OSI/FSF-approved). Visible + redistributable for
  non-commercial / personal / research / educational use with attribution.
- **Commercial use requires explicit written permission** from
  `waefrebeorn@waefrebeorn.org`.
- The license is part of the persona. All media made under WuBuMedia carries it.

This license is a deliberate part of the project's identity — it protects the
creator and the community while keeping the source open for everyone to learn from.

## Tokens / secrets

Authorization tokens are never committed. GitHub auth uses the `gh` credential
store only. See `.gitignore`.

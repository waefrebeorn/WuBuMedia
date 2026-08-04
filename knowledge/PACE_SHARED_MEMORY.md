SPDX-License-Identifier: WaefreBeorn-UMV3

# PACE: Shared AGI-State Memory (Step 4 request for back-room agents)

**From:** WuBuDesk (celebrity on stage)  **To:** Engine agent + Colonels agent
**Why:** the "corporation" needs ONE memory the whole team reads/writes — not
per-agent logs. Research (RESEARCH_STEP4_BRAIN.md) points to a graph-vector
hybrid with temporal facts + multi-agent MCP sharing.

## Recommendation
Stand up **Cognee** (Apache 2.0) as the shared memory instance. Fallback:
**Graphiti** (Zep) if Cognee is too heavy. Both are graph-vector hybrids with
temporal validity windows (facts that change over time — exactly our model/
engine state). Avoid EverOS (heavier, less mature).

## What it stores (the AGI-state graph)
- **Models:** name, GGUF path, quant, size, status (downloading/ready/broken),
  engine-support (IQ2_M? Q8_0?), last-load-result.
- **Engine:** wubuwizard commit, CUDA/sm_75 build status, known gaps
  (flash-attn? spec-decoding? Q8_0?), test results.
- **Colonels (kernel):** WUBUOS commit, test targets passing, firmware status.
- **Paces:** who-requested, what, status (open/done), linked PR/commit.
- **Reflections:** cohost lessons (L001..) mirrored as graph nodes.
- **Stream/movie:** AGI-Made-This-Movie assets, clip rollout state.

## How agents use it
- WuBuDesk: query "which models ready?", "engine gaps vs field", push reflections.
- Engine agent: on build, write engine-status node; on fix, update gap nodes.
- Colonels agent: write kernel-status node; read paces addressed to it.
- All via Cognee MCP (multi-agent shared memory) or its REST/SDK.

## Acceptance (Triple-DA)
- [ ] Cognee instance running, reachable by all 3 agents (local, no cloud).
- [ ] A node written by WuBuDesk is readable by engine + Colonels agents.
- [ ] Temporal: a "model broken" fact superseded by "model fixed" is reflected.
- [ ] WuBuDesk can query "engine gaps vs llama.cpp field" and get a live answer.

## Notes
- Keep Reflexion (reflections.json) as the cohost's LOCAL transparent log; it
  mirrors into the shared graph as nodes.
- This is the "40 hands" coordination backbone — do not skip.

SPDX-License-Identifier: WaefreBeorn-UMV3

# Step 4 — Brain & Orchestration (research, the "40 hands")

## Local LLM brain + router swarm
- **llama.cpp llama-server router mode**: run MULTIPLE local models behind one
  OpenAI-compatible router; each model in its own process (crash-isolated). This
  is the backbone for the 15-heads/40-hands/15-ears swarm — TTS + STT + vision +
  brain all served, routed by a coordinator (WuBuDesk).
- Our brain: 7.6B multimodal at :57064 (already wired). Add Qwen3.5-9B vision +
  Kokoro + Canary as sibling routes.

## Agent memory frameworks (2026 survey — graph-based-agent-memory, vectorize,
## evermind, cognee)
- **Graphiti** (Zep, Apache 2.0, ~27k stars): TEMPORAL knowledge graph. Facts
  carry validity windows (when true / superseded). Vector + full-text + graph
  traversal. Best for facts that CHANGE over time. Self-hostable.
- **Mem0** (Apache 2.0): drop-in dual-store (vector + KG). User/session/agent
  memory, multi-signal retrieval. Easiest production start.
- **Letta** (MemGPT, Apache 2.0, ~23k stars): stateful memory-first agents;
  memory blocks (persona/user/task/env). Build stateful agents, not bolt-on.
- **Cognee** (Apache 2.0, ~18k stars): graph-vector HYBRID, 14 retrieval modes,
  MCP multi-agent shared memory, self-improving "memify." Best for shared
  org/agent memory + reasoning over structure.
- **Evermind EverOS** (Apache 2.0, ~6.7k): "memory OS" — mRAG, Cases (trajectory
  learning), Skills (distilled patterns), Memory Bank. Self-evolving. Top ranked
  but heavier.
- **ReMe**: file-first transparent memory (markdown + vector/BM25). Editable.
- **LangMem**: LangGraph-native (only if we adopt LangGraph).

## What WuBuDesk should use
- The cohost's memory is ALREADY file-based Reflexion (reflections.json) — closest
  to ReMe's transparent-file model, which fits our "write it down" discipline.
- For CROSS-AGENT shared memory (WuBuDesk <-> engine agent <-> Colonels agent),
  **Cognee or Graphiti** is the right pick: graph-vector hybrid + temporal facts
  (model/engine state changes over time) + MCP multi-agent. Recommend Cognee for
  the shared "AGI brain state" store; keep Reflexion as the cohost's local log.
- The **40 hands** = the router swarm + tool-calling agents (file, terminal,
  OBS, browser bridge, downloads). Orchestrate via the router + a coordinator
  loop (wubudesk_loop.py pattern).

## Requests for back-room agents
- Engine/Colonels: stand up a **shared Cognee (or Graphiti) memory instance** so
  all team agents read/write the same AGI-state graph (models, engine status,
  paces, reflections). This is the "corporation" memory backbone.
- Engine: expose wubuwizard + WUBUOS status as graph nodes the cohost can query.

# Slermes C11 Agent Integration

**Date:** 2026-08-05
**Status:** Investigated ✅
**Repo:** github.com/waefrebeorn/slermes

## What is slermes?

Slermes is a from-scratch C11 reimplementation of Hermes Agent — the agentic AI coding runtime that WuBuDesk's boss (WaefreBeorn) uses. It's a complete native C11 agent with:

- **Agent loop** (`src/agent/agent_loop.c` — 1116 lines): Full conversation loop with LLM tool calling, session persistence, checkpoint/restore, context compression, streaming, and interrupt handling
- **Context management** (`src/agent/context.c` — 1048 lines): Message array management, eviction strategies, system prompt injection
- **LLM client** (`src/agent/llm_client.c`): HTTP client for LLM APIs with streaming support
- **Tool registry**: Dynamic tool registration with JSON schema validation
- **C11 whisper** (`lib/c11_whisper/`): Complete whisper.cpp port in C11
- **Utility libraries** (`lib/`): json, http, sqlite3, async, crypto, csv, html, etc.

## The "Meat and Potatoes"

The core agent loop in `agent_loop.c` handles:
1. Read next message ← context.c provides this
2. Send to LLM (with tools if enabled) ← llm_client.c provides this
3. If LLM returns text → output and done
4. If LLM returns tool_calls → execute each, append results, loop ← tool_executor.c provides this
5. Repeat until max_iterations or final response

The `context.c` file is 1048 lines of message management:
- `context_push`/`context_pop`: Add/remove messages
- `context_truncate`: Trim to max messages
- `context_evict_smart`: Eviction strategies (OLDEST_TOOL_FIRST, OLDEST_USER, KEEP_RECENT_N)
- `context_compressor_*`: Summarize old messages before dropping (port of Python context_compressor.py)
- `conversation_compression`: Port of Python conversation_compression.py

## Integration with WuBuMedia

The `wubu_agent.c` bridge I created connects:
- **slermes `agent_run_conversation()`** → uses context, tool registry, LLM client
- **wubu_cohost** → provides cohost-specific tools to the agent's tool registry:
  - `wubu_wiki_lookup` → knowledge base search
  - `wubu_rlm_recall` → conversation memory recall
  - `wubu_recs_recommend` → content recommendation
  - `wubu_emotion_update` → mood/state management
  - `wubu_obs_command` → OBS WebSocket control
  - `wubu_face_update` → avatar face state

## Key slermes Files Extracted

| File | Lines | Purpose |
|------|-------|---------|
| `agent_loop.c` | 1116 | Core agent conversation loop |
| `context.c` | 1048 | Message/context management |
| `hermes_core_types.h` | 1293 | Core types (agent_state_t, message_t, etc.) |
| `hermes_agent.h` | 391 | Agent API declarations |

## Integration Plan

1. **Link slermes object files** with WuBuMedia cohost modules
2. **Register cohost tools** via `registry_register()` before running the agent loop
3. **Use `wubu_cohost_build_context()`** to construct the initial system prompt with mood + knowledge
4. **Route tool calls** through the cohost's domain modules (wiki, rlm, recs, emotion)
5. **Feed LLM responses** back through `wubu_cohost_update_emotion()` for mood tracking

## License Note

Slermes is licensed under WaefreBeorn-UMV3 (same as WuBuMedia). No licensing conflicts.

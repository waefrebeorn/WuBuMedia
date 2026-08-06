# TikTok FYP Algorithm Research

**Date:** 2026-08-05
**Source:** User-provided research (verified against OSS solutions report)
**Status:** Verified — TikTok FYP is proprietary, not open source

---

## Key Finding
**TikTok's full FYP recommendation algorithm is proprietary and not open source.** ByteDance has never released the complete production code or exact weights. No complete reverse-engineered copy exists in public repositories.

## What IS Available (Verified)

### 1. ByteDance's Monolith Framework
- **GitHub:** https://github.com/bytedance/monolith (public archive as of October 2025)
- **Paper:** "Monolith: Real Time Recommendation System With Collisionless Embedding Table" (ORSUM@ACM RecSys 2022)
- **Key innovations:**
  - Collisionless embedding tables (Cuckoo HashMap-based, with expirable embeddings)
  - Online/real-time training that updates on user feedback streams
  - High fault-tolerance parameter servers
  - Support for sparse/dynamic features
  - Runs on TensorFlow

### 2. Internal Description — "TikTok Algo 101"
**Simplified scoring formula (real one is more complex):**
```
P_like × V_like + P_comment × V_comment + E_playtime × V_playtime + P_play × V_play
```
Videos are scored; highest-scoring returned. System optimizes for retention and time spent, with deliberate diversity/exploration injection.

### 3. Live System Architecture (Multi-stage)
1. **Candidate retrieval** — Two-tower/multi-tower embedding models pulling thousands of candidates from hundreds of millions of videos
2. **Ranking** — Heavy ranking model scoring candidates using hundreds of features
3. **Re-ranking/filtering** — Business rules, diversity, freshness, safety, exploration

**Signal hierarchy:**
- Highest weight: Watch time/completion rate, rewatches, shares, saves, comments (quality-weighted), likes, follows, "Not Interested"
- Medium weight: Video metadata (captions, on-screen text, hashtags, sounds, effects, duration)
- Lowest weight: Device/account settings (language, country, device type)

**Mechanism:** New videos tested in small "buckets"; strong early engagement expands audience. User embeddings updated near real-time.

## Open-Source Approximations

| Project | Description | Link |
|---------|-------------|------|
| **pydata-tiktok** | Feature pipeline, two-tower retrieval, ranking, Streamlit UI. PyData Berlin 2024 tutorial. | github.com/jimdowling/pydata-tiktok |
| **rectik** | Multi-stage TikTok-style recommender using NVIDIA Merlin + Metaflow + Feast + FAISS + Triton | github.com/LongBaoCoder2/rectik |
| **tiktokx** | Multi-modal (video+audio+text) recommender with generative self-augmentation and contrastive learning | github.com/kyegomez/tiktokx |
| **shorts** | Pure content-based recommender from raw video files (Gemini embeddings + cosine similarity) | github.com/yodiaditya/shorts |
| **tiktok-audit** | Translated "Algo 101" document + audit tooling | github.com/mrtn3000/tiktok-audit |

## Closely Related Open-Source Recommendation Algorithms

### X/Twitter the-algorithm
- **GitHub:** https://github.com/twitter/the-algorithm + https://github.com/twitter/the-algorithm-ml
- **Full open-source timeline ranking + heavy ranker**
- Similar multi-stage architecture
- **License:** AGPL-3.0
- **Language:** Python/Thrift
- **C11 wrapper verdict:** ❌ Not wrappable — port algorithm to C11

## C11 Implementation Strategy for WuBuDesk AGI

### Approach: Re-implement the algorithm in C11
1. **Use SQLite** as the event-log store (already integrated via `wubu_wiki.c`)
2. **Implement the scoring formula** in C11 against SQLite event data
3. **Two-stage retrieval:**
   - Stage 1: Simple cosine similarity over TF-IDF features (C11, fast)
   - Stage 2: Heavy ranking using wubuwizard/llama.cpp C API (C11-compatible)
4. **Real-time updates:** Append-only event log, periodic re-embedding
5. **Diversity/exploration:** Inject random candidates based on exploration rate

### Mapping to WuBuDesk Problem #1 (Stream Content Selection)
- **Input:** User interaction events (watch time, likes, shares, saves, comments)
- **Feature store:** SQLite tables in `wiki.db` or separate `recs.db`
- **Model:** Two-tower embedding (user tower + video tower) — lightweight C11 implementation
- **Output:** Ranked list of content recommendations

### Mapping to WuBuDesk Problem #2 (C11 Daemon for Knowledge Base)
- The recommendation engine can share the `wubu_daemon` named pipe IPC
- Expose `rec_score`, `rec_candidates`, `rec_experiment` commands
- SQLite WAL mode ensures concurrent access with the wiki daemon

## Bottom Line
You cannot download "the" TikTok algorithm. Build something that behaves like TikTok by:
1. Starting with Monolith framework concepts or educational systems
2. Feeding short-video interaction data
3. Implementing continuous online training
4. Using strong completion-rate/share signals

For WuBuDesk: implement the **scoring formula** and **multi-stage architecture** in C11, backed by SQLite event logs, with wubuwizard handling heavy model inference.

## References
- `knowledge/OSS_SOLUTIONS_WUBUDESK.md` — Solution 1 (X RecSys) + Solution 10 (SQLite)
- `knowledge/WUBUDESK_10_PROBLEMS_REPORT.md` — Problem 1 (C11 Daemon KB)
- Monolith paper: Cuckoo HashMap, collisionless embeddings
- `github.com/mrtn3000/tiktok-audit` — translated Algo 101 document

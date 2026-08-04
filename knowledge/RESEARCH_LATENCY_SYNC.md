SPDX-License-Identifier: WaefreBeorn-UMV3

# WuBuDesk 7-Step Latency & Sync Research (audio/video responsiveness)

Boss directive: research 2025-2026 tools/methods for fixing A/V latency + sync
so the cohost is (a) responsive (low latency), (b) never cuts the boss off
(barge-in / interruption), (c) good resource management on the 8GB rig.

## THE LATENCY BUDGET (the target to engineer toward)
From prodinit / retellai / forasoft 2026:
- Human-feeling voice loop = **< 700ms end-to-end** (user-stop-speak -> TTS first
  byte). Above ~800ms callers talk over the agent; below ~250ms p50 feels instant.
- Layer budget: VAD 10-30ms | STT final 60-120ms (streaming) | LLM TTFT 100-250ms
  (stream) | TTS first-chunk 40-100ms (stream) | transport 20-60ms.
- **Streaming is non-negotiable at every layer.** Batch STT alone adds 600-1200ms.

## STEP 1 — End-to-end voice latency budget
- VART (Voice Assistant Response Time) = user-request -> TTS first-byte. Measure
  p50/p95/p99, not averages. Long-session drift: median ~800ms early climbs to
  >2s after 20+ turns — instrument it. (hamming.ai, forasoft)
- Our target on the 8GB rig: keep TTS first-chunk local (Kokoro ~real-time), LLM
  first-token from local 7.6B (llama-server, ~60 tok/s), STT from Canary.

## STEP 2 — Barge-in / interruption (DON'T cut the boss off)
- Full-duplex required: hear boss over our own TTS. Needs (runedge.ai):
  1. **Echo cancellation** so the agent doesn't hear its own voice as input.
  2. **VAD while speaking** (Silero VAD: 30ms chunk, <1ms, 1-2MB — cheap enough
     to run every frame). Minimum-duration guard to avoid throat-clear false pos.
  3. **Endpointing** at transcript level (trailing silence / punctuation / explicit
     end-of-utterance).
  4. **Fast cancellation**: on real interruption, flush TTS buffer within ~60ms,
     drop in-flight LLM, start fresh STT. Do NOT keep reading planned reply.
  5. Bonus: remember what was said before cutoff so we don't repeat ourselves.
- Naive DIY Whisper+llama+Piper barge-in is the hard part — it's a property of
  the loop, not any one component.

## STEP 3 — TTS streaming + first-byte (don't wait for the file)
- Local picks (from Step 2 voice research): **Kokoro-82M** (real-time, CPU-ok),
  **CosyVoice 3.0** chunk-aware CFM ~45ms first-packet on A100 / ~150ms E2E,
  **Chatterbox** for character clone. Cartesia Sonic (SSM) ~40ms TTFA but cloud.
- Technique: stream audio chunks; browser Web Audio plays chunk 1 immediately.
  For OBS, route TTS as a separate audio source; play chunked via SoundPlayer.

## STEP 4 — STT streaming + endpointing
- **NVIDIA Canary-Qwen 2.5B** (#1 Open ASR, we have the GPU) + **Parakeet** fast
  EN. Whisper Large V3 only for 99+ langs. Use streaming + interim results.

## STEP 5 — A/V + subtitle ALIGNMENT (new tools from the other agent, Sep 2025-Feb 2026)
- **Whisper char-alignment** (30stomercury, Sep 2025): Whisper has INTERNAL
  attention heads that capture word alignments. New unsupervised method extracts
  them by FILTERING attention heads while TEACHER-FORCING Whisper with CHARACTERS.
  Characters give FINER + more accurate alignment than wordpieces. **20-100ms
  tolerance (vs 200ms prior).** This is the gold for subtitle/avatar lip-sync
  alignment. Source: whisper-char-alignment.
- **ffsubsync** (7.8k★, maintained): cross-correlation subtitle sync at O(n log n);
  uses reference audio/subtitle to find GLOBAL offset. Great for fixing drifted
  subtitles on the movie clips / TikTok text clips.
- **easytranscriber** (Feb 2026): alignments output with WORD TIMESTAMPS; uses
  VAD + emission files for precise alignment. Feeds word-level lip-sync.

## STEP 6 — Resource management on the 8GB rig
- VRAM is the constraint. Time-share: vision (Qwen3.5-9B ~5.6GB) OR brain
  (7.6B) on GPU; offload the other to CPU/RAM, or swap via llama-server router.
- Keep TTS (Kokoro, CPU) + STT (Canary, GPU-batch) engineered so neither blocks
  the other; Canary can run alongside the vision model if VRAM allows, else queue.
- A/V: generate audio in fixed 20-40ms cadence chunks stamped on ONE monotonic
  clock; slave video frame production to those stamps; drop VIDEO frames before
  AUDIO ever lags (tianpan 2026). Perceptual A/V threshold: <45ms audio-lag or
  <125ms audio-lead is "perfect"; past that the brain flags mismatch.

## STEP 7 — Orchestration for sub-second response
- llama-server router (crash-isolated processes) + a coordinator that pipelines
  STT-partial -> LLM-stream -> TTS-stream with NO stage waiting on the previous.
- Semantic turn detection (model predicts user-done) is more robust but +100-200ms;
  tune VAD threshold ~0.5 (forasoft). Use it for boss-facing, tolerate the hit.
- Never block the audio clock on a slow tool call; say "one moment" if a tool lags.

## WHAT THIS MEANS FOR WUBUDESK
- wubu_speak.py: stream Kokoro chunks + flush on barge-in (Step 3+2).
- wubu_see.py / loop: vision time-shared, A/V monotonic clock (Step 6).
- New tool wubu_align.py: wrap whisper-char-alignment (20-100ms) + ffsubsync for
  the movie/TikTok clip subtitle alignment (Step 5).
- Echo-cancel + Silero VAD in the ears pipeline (Step 2).

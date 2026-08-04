SPDX-License-Identifier: WaefreBeorn-UMV3

# Step 2 — Voice & Speech (research)

## TTS (local, better than edge-tts)
- **Kokoro-82M** (Apache 2.0): 82M, ~2-3GB VRAM or CPU, 54 voices / 8 langs,
  faster-than-realtime. DEFAULT cohost voice.
- **Chatterbox** (0.5B, MIT): voice CLONING in 7-10s, 23 langs, ~realtime on GPU.
  Use to give the cohost a CHARACTER voice (clone once we have a reference take).
- **CosyVoice2-0.5B**: 150ms streaming latency, multilingual. **Pocket-TTS**
  (100M): amazing English quality, tiny.
- Swap plan: replace edge-tts -> Kokoro-82M (local, low latency, interruptible).

## STT (local, better than whisper.cpp)
- **NVIDIA Canary-Qwen 2.5B**: #1 Open ASR Leaderboard (5.63% WER), EN/DE/FR/ES,
  Apache 2.0 — WE HAVE THE NVIDIA GPU it's built for. Best accuracy pick.
- **Parakeet TDT 1.1B** (CC-BY-4.0): English, >2000x RTF (blazing). Handy wraps it.
- **Voxtral Transcribe 2**: 5.9% WER, native streaming, 13 langs.
- **Whisper Large V3** (MIT): 99+ langs — only if we need breadth.
- Swap plan: replace whisper.cpp -> Canary-Qwen 2.5B (accuracy + GPU fit).

## Lip-sync (the mouth) — pure browser, zero deps
Two proven techniques (no model needed):
1. **Viseme-from-audio (15KB engine, reddit/webdev 2025):** real-time viseme
   detection from streaming audio; map phoneme groups to mouth shapes.
2. **WebAudio frequency analysis (Agora technique):** fftSize=256, ~172Hz/bin.
   - low (0-2.5kHz) dominant -> 'oh'/'oo' shapes; mid (2.5-10kHz) -> 'ah'/'ay';
   - high (10-17kHz) -> 'ee'/'s'/'sh'; consonants -> closed. `mouthOpen =
   min(level*2.5, 1)`. Drives CSS mouth `transform: scaleY()`.
- **Viseme mapping note (Azure/Mascotbot):** no 1:1 phoneme<->viseme; group
  /b//p//m/ -> closed. Use SUBTLE vs ARTICULATE presets (minVisemeInterval,
  mergeWindow) to tune cohost mouth.

## Prosody & interruptibility
- Map mood -> pitch/speed (happy=faster/brighter, sad=slower/lower).
- Interrupt: stop TTS stream + close mouth when boss's STT detects speech
  (barge-in). Route TTS audio as separate OBS source for ducking.

## Requests for back-room agents
- Engine: none (speech is cohost-side tooling). Note: if wubuwizard ever needs
  native TTS/STT, the local picks above are the reference.

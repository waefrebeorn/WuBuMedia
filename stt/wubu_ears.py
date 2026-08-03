#!/usr/bin/env python3
"""
wubu_ears.py — the cohost's local listening sense (whisper on the boss's audio).

Captures microphone (and optionally system audio) on Windows 11 via
sounddevice/WASAPI and transcribes locally with faster-whisper (CTranslate2,
CUDA on this RTX 2080). No cloud. Low latency with turbo model.

This is the CUA "hear" part: the boss talks (to chat, to the game, to me), the
cohost hears and understands — then uses vision/desktop to act.

Usage:
  wubu_ears.py --listen 5     # listen 5s from mic and transcribe
  wubu_ears.py --devices      # list audio devices
  wubu_ears.py --file x.wav   # transcribe a file

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import argparse
import sys
import numpy as np
import sounddevice as sd

MODEL = "large-v3-turbo"  # best speed/accuracy local; swap to "base.en" for tiny


def load_model():
    from faster_whisper import WhisperModel
    # device="cuda" uses the RTX 2080; compute_type float16 halves VRAM
    return WhisperModel(MODEL, device="cuda", compute_type="float16")


def transcribe(model, audio: np.ndarray):
    audio = audio.astype(np.float32)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    segments, info = model.transcribe(audio, beam_size=1, language="en")
    text = " ".join(s.text.strip() for s in segments)
    return text


def list_devices():
    print("=== audio devices ===")
    for i, d in enumerate(sd.query_devices()):
        print(f"  [{i}] {d['name']} (in={d['max_input_channels']} out={d['max_output_channels']})")


def listen_seconds(model, seconds, samplerate=16000, device=None):
    print(f"[ears] listening {seconds}s (mic)...")
    audio = sd.rec(int(seconds * samplerate), samplerate=samplerate, channels=1,
                   dtype="float32", device=device)
    sd.wait()
    text = transcribe(model, audio[:, 0])
    print(f"[ears] heard: {text}")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--devices", action="store_true")
    ap.add_argument("--listen", type=int, default=0)
    ap.add_argument("--file")
    ap.add_argument("--device", type=int, default=None)
    args = ap.parse_args()

    if args.devices:
        list_devices()
        return
    if not (args.listen or args.file):
        list_devices()
        print("use --listen <secs> or --file x.wav")
        return

    print("[ears] loading model...")
    model = load_model()
    if args.file:
        import soundfile as sf
        data, sr = sf.read(args.file)
        print(f"[ears] transcribed file: {transcribe(model, data)}")
    if args.listen:
        listen_seconds(model, args.listen, device=args.device)


if __name__ == "__main__":
    main()

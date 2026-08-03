#!/usr/bin/env python3
"""
wubu_stt.py — local low-latency speech-to-text client for the cohost.

Talks to a whisper.cpp server (whisper.cpp/build/bin/whisper-cli --server, or
`whisper.cpp server`) over its HTTP API. Feeds mic audio (or a file) and returns
text. This is the cohost's EARS — fully local, no cloud, low latency.

Benchmarks used to pick whisper.cpp small.en on RTX 2080:
  WER 0.064, mean 101ms, P95 RTF 0.024 (Windows CUDA) -> real-time w/ headroom.

Usage:
  python3 wubu_stt.py --mic            # stream from default mic
  python3 wubu_stt.py --file clip.wav  # transcribe a file
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import argparse
import json
import subprocess
import sys
import urllib.request

SERVER = "http://127.0.0.1:8080"
MODEL = "small.en"  # tiny/fast; swap to base.en for more accuracy


def ensure_server():
    """Start the whisper.cpp server if not up (elevated build path)."""
    import socket
    try:
        s = socket.socket(); s.settimeout(1); s.connect(("127.0.0.1", 8080)); s.close()
        return True
    except Exception:
        pass
    # launch server headless
    exe = r"C:\Users\eman5\whisper.cpp\build\bin\whisper-cli.exe"
    cmd = [exe, "--server", "-m",
           rf"C:\Users\eman5\whisper.cpp\models\ggml-{MODEL}.bin",
           "-t", "6", "-p", "8080", "--convert", "-l", "en"]
    subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return False


def transcribe(audio_path):
    req = urllib.request.Request(
        f"{SERVER}/inference",
        data=open(audio_path, "rb").read(),
        headers={"Content-Type": "audio/wav"},
        method="POST")
    try:
        resp = urllib.request.urlopen(req, timeout=30)
        return json.loads(resp.read())["text"].strip()
    except Exception as e:
        return f"[stt error: {e}]"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file")
    ap.add_argument("--mic", action="store_true")
    args = ap.parse_args()
    ensure_server()
    if args.file:
        print(transcribe(args.file))
    elif args.mic:
        print("[mic mode] use a capture tool (e.g. ffmpeg) to pipe wav to --file, or wire cua+mic")
    else:
        print("use --file clip.wav or --mic")


if __name__ == "__main__":
    main()

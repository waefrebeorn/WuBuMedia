#!/usr/bin/env python3
"""
wubu_voice.py -- the cohost's actual voice. Piper on CPU, RVC-ready.

Boss: "you need to have my rvc voices and use them to talk to me best fast
amazing i have so many" + "fix them to work with the sota 2026 tts."

WHY THIS SHAPE (research 2026-08-05, full report in ~/rvc_tts_2026_research.md):
  * The 8 GB card is already carrying OBS + NVENC + NVIDIA Broadcast. Anything
    resident on the GPU steals from the encoder, which is what made the rig lag
    earlier today. So the whole voice path runs on CPU.
  * Zero-shot cloners (Chatterbox 2.5-16 GB, F5-TTS 4.2 GB, Fish-Speech 12 GB)
    are ruled out on VRAM alone -- AND none of them can reproduce the boss's
    152 *trained* RVC speakers. Those models are assets; keep RVC.
  * Piper medium on CPU is the right source: measured on THIS rig
    531 ms for a 3.2 s sentence (RTF 0.166) once warm. First call costs ~4.4 s
    of ONNX warmup, so we pre-warm at boot and never pay it on stream.
  * Pitch extraction: use rmvpe (RTF 0.033) -- crepe is 14.5x slower
    (arXiv 2509.15140 Table 2). Never crepe for realtime.

DESIGN
  Voice.say(text) -> wav path, synthesized on a worker thread so the reply
  never blocks the cohost loop. RVC conversion is a pluggable post-step: when
  an engine is wired up it converts Piper's output into one of the boss's
  152 voices; until then the Piper audio plays as-is. Either way the cohost
  talks.

  The resource guard still rules: if the rig is STREAMING/GAMING the caller
  decides whether to speak. This module never grabs the GPU.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import queue
import subprocess
import sys
import threading
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "out", "speech")
PIPER_MODEL = os.environ.get("WUBU_PIPER") or \
    os.path.join(ROOT, "models", "piper", "en_US-ryan-medium.onnx")


class Voice:
    """Speak on a worker thread. CPU only. Never raises into the loop."""

    def __init__(self, model=PIPER_MODEL, rvc=None, play=True,
                 voice_name="WheatleyV2"):
        self.model_path = model
        self.rvc = rvc              # optional callable: wav_in, name -> wav_out
        self.voice_name = voice_name
        self.play = play
        self.q = queue.Queue()
        self.voice = None
        self.ready = False
        self.error = None
        self.spoken = 0
        self.last_ms = 0
        self.speaking = threading.Event()
        os.makedirs(OUT, exist_ok=True)
        self.worker = threading.Thread(target=self._run, name="voice",
                                       daemon=True)

    def start(self):
        self.worker.start()
        return self

    # -- engine ----------------------------------------------------------
    def _load(self):
        try:
            from piper import PiperVoice
        except Exception as e:
            self.error = f"piper missing: {e}"
            return False
        if not os.path.exists(self.model_path):
            self.error = f"no piper model at {self.model_path}"
            return False
        try:
            t = time.time()
            self.voice = PiperVoice.load(self.model_path)
            # pre-warm: the first synth costs ~4.4s of ONNX graph warmup and we
            # are NOT paying that in the middle of a live reply.
            warm = os.path.join(OUT, "_warm.wav")
            with wave.open(warm, "wb") as w:
                self.voice.synthesize_wav("warming up", w)
            print(f"[voice] piper ready in {time.time() - t:.1f}s "
                  f"({os.path.basename(self.model_path)})", flush=True)
            self.ready = True
            return True
        except Exception as e:
            self.error = f"piper load failed: {e}"
            return False

    def _synth(self, text):
        path = os.path.join(OUT, f"say_{int(time.time() * 1000)}.wav")
        with wave.open(path, "wb") as w:
            self.voice.synthesize_wav(text, w)
        return path

    def _playback(self, path):
        """Play through a captureable device so OBS hears the cohost.

        Routes to Voicemeeter Input (device 9) -- that bus feeds OBS. If it's
        not available, fall back to default speakers; never block the loop."""
        import sounddevice as sd
        import soundfile as sf
        try:
            data, sr = sf.read(path, dtype="float32")
        except Exception as e:
            print("[voice] read failed:", str(e)[:60], flush=True)
            return False
        # 9 = Voicemeeter Input (VB-Audio). OBS captures this bus, so the
        # cohost's voice goes on the stream instead of into the void.
        for dev in (9, None):
            try:
                sd.play(data, sr, device=dev)
                sd.wait()
                if dev is not None:
                    return True
            except Exception as e:
                if dev is not None:
                    print("[voice] dev 9 failed, falling back:",
                          str(e)[:60], flush=True)
                    continue
                return False
        return False

    def _run(self):
        if not self._load():
            print("[voice] DISABLED:", self.error, flush=True)
            return
        while True:
            text = self.q.get()
            if text is None:
                return
            try:
                t = time.time()
                self.speaking.set()
                wav = self._synth(text)
                if self.rvc:                 # convert into one of the 152 voices
                    try:
                        wav = self.rvc(wav, self.voice_name) or wav
                    except Exception as e:
                        print("[voice] rvc failed:", str(e)[:70], flush=True)
                if self.play:
                    self._playback(wav)
                self.last_ms = int((time.time() - t) * 1000)
                self.spoken += 1
            except Exception as e:
                self.error = str(e)[:120]
                print("[voice] error:", self.error, flush=True)
            finally:
                self.speaking.clear()

    # -- api -------------------------------------------------------------
    def say(self, text, max_chars=240):
        """Queue a line. Drops if the engine never loaded -- never blocks."""
        if not text or not self.ready:
            return False
        if self.q.qsize() > 2:      # stale backlog: a cohost interrupts itself
            try:
                while self.q.qsize() > 1:
                    self.q.get_nowait()
            except Exception:
                pass
        self.q.put(text[:max_chars])
        return True

    def busy(self):
        return self.speaking.is_set() or not self.q.empty()


if __name__ == "__main__":
    v = Voice(play=("--silent" not in sys.argv)).start()
    for _ in range(40):
        if v.ready or v.error:
            break
        time.sleep(0.5)
    if not v.ready:
        raise SystemExit(f"voice not ready: {v.error}")
    lines = sys.argv[1:] or ["Skill issue, honestly.",
                             "Got what? A loot drop or another glitch?"]
    for line in [l for l in lines if not l.startswith("--")]:
        t = time.time()
        v.say(line)
        while v.busy():
            time.sleep(0.05)
        print(f"  {v.last_ms:5d}ms  {line!r}")
    print("spoken:", v.spoken, "err:", v.error)

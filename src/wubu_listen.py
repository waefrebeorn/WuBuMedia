#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_listen.py — WuBuDesk ears + barge-in (Step 2 of the plan).

Barge-in: while the cohost is speaking, continuously VAD-scan the mic. If the
boss starts talking, set the stop Event so wubu_speak aborts its TTS stream
within ~60ms (don't cut the boss off). Full-duplex: we hear over our own voice.

Best-effort: if deps (sounddevice / silero) are missing, methods degrade to
no-op so the rest of the cohost keeps running.

Runs on the Windows venv at WuBuMedia/.venv_win.
"""
import os
import sys
import time
import threading

VENV = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".venv_win",
                                    "Scripts", "python.exe"))

_SR = 16000
_CHUNK = 512  # ~32ms @16k


def _have_mic_stack():
    try:
        import sounddevice  # noqa
        return True
    except Exception:
        return False


class BargeIn(threading.Thread):
    """VAD watcher: sets `stop` when boss speech is detected during cohost speech."""
    def __init__(self, stop_event, threshold=0.5):
        super().__init__(daemon=True)
        self.stop = stop_event
        self.threshold = threshold

    def run(self):
        if not _have_mic_stack():
            return
        try:
            import sounddevice as sd
            import numpy as np
            from silero_vad import load_silero_vad, get_speech_timestamps
            model = load_silero_vad()
            def cb(indata, frames, t, status):
                if self.stop.is_set():
                    return
                audio = np.frombuffer(indata, dtype=np.int16).astype(np.float32) / 32768.0
                ts = get_speech_timestamps(audio, model, sampling_rate=_SR,
                                           threshold=self.threshold, min_speech_duration_ms=200)
                if ts:
                    self.stop.set()
            with sd.InputStream(samplerate=_SR, channels=1, blocksize=_CHUNK,
                               dtype="int16", callback=cb):
                while not self.stop.is_set():
                    time.sleep(0.05)
        except Exception:
            pass


def capture_utterance(timeout=4):
    """Listen up to `timeout` seconds, return transcribed text (or None)."""
    if not _have_mic_stack():
        return None
    try:
        import sounddevice as sd
        import numpy as np
        from faster_whisper import WhisperModel
        buf = []
        def cb(indata, frames, t, status):
            buf.append(np.frombuffer(indata, dtype=np.int16).copy())
        with sd.InputStream(samplerate=_SR, channels=1, blocksize=_CHUNK,
                           dtype="int16", callback=cb):
            time.sleep(timeout)
        audio = np.concatenate(buf) if buf else np.zeros(_SR, dtype=np.int16)
        m = WhisperModel("tiny", device="cpu", compute_type="int8")
        segs, _ = m.transcribe(audio.tobytes(), language="en")
        return " ".join(s.text for s in segs).strip() or None
    except Exception as e:
        print("capture_utterance error:", e)
        return None


if __name__ == "__main__":
    print("mic stack:", _have_mic_stack())
    print("capture_utterance(3) ->", capture_utterance(3))

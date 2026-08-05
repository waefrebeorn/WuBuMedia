#!/usr/bin/env python3
"""
wubu_ears.py -- continuous hearing for the cohost. Self-contained.

Boss called it 2026-08-04, LIVE: "I have been talking this whole time, you are a
non existent cohost." He was right -- wubu_listen.py only ever exposed a 4-second
one-shot that nothing in the loop called. This module listens ALWAYS, in a
background thread, and hands finished utterances to a callback.

Design:
  * ring buffer + RMS gate -> no torch/VAD needed on the hot path (cheap while
    the boss is streaming; the resource guard already forbids GPU work).
  * an utterance = speech onset -> SILENCE_HOLD seconds of quiet. That gets us
    natural sentence boundaries instead of arbitrary 4s windows.
  * faster-whisper base.en on CPU int8: ~0.4s for a short line on this rig, and
    critically 0 VRAM -- the 8 GB card belongs to the stream encoder.
  * Device defaults to NVIDIA Broadcast (noise-suppressed, same as OBS). Verified
    on-rig: device 22, RMS 0.038, clean transcript.

Never raises into the live loop: every failure degrades to "no ears".
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import threading
import time

_HERMES_SITE = (r"C:\Users\eman5\AppData\Local\hermes\hermes-agent"
                r"\venv\Lib\site-packages")
if os.path.isdir(_HERMES_SITE) and _HERMES_SITE not in sys.path:
    # Append (NOT insert at 0) -- our own venv ships CUDA torch + piper and
    # must win. Hermes' site-packages only provides audio deps (sounddevice,
    # numpy, faster-whisper) that we don't otherwise have.
    sys.path.append(_HERMES_SITE)
_HOME = os.environ.get("USERPROFILE") or os.environ.get("HOME") or r"C:\Users\eman5"
os.environ.setdefault("HOME", _HOME)
os.environ.setdefault("USERPROFILE", _HOME)

SR = 16000
BLOCK = 1600               # 100 ms
RMS_GATE = 0.004           # floor; real gate is calibrated to the room at start
SILENCE_HOLD = 0.9         # quiet this long ends the utterance
MIN_UTTERANCE = 0.6        # ignore coughs/clicks
MAX_UTTERANCE = 15.0       # hard cap so a monologue still flushes
CALIB_SECS = 1.5           # measure room noise, then gate above it
MODEL_SIZE = os.environ.get("WUBU_STT_MODEL") or "base.en"

# Prosodic feature extraction for emotion detection.
# Research: pitch, energy, and speech rate are the three pillars of
# real-time emotion recognition (see Inworld, Forosoft SER papers).
# We use lightweight numpy-based proxies (no librosa) since the resource
# guard forbids heavy CPU work during streaming.
PROSODY_SAMPLES = 8  # number of recent blocks to analyze for pitch/energy trend


def extract_prosody(block, sr=SR):
    """Extract lightweight prosodic features from a 100ms audio block.

    Returns dict with: rms (energy), zcr (pitch proxy), pitch_hz,
    energy_std (energy variability). All computed with numpy only.
    """
    import numpy as np
    mono = block.reshape(-1)
    if len(mono) == 0:
        return {"rms": 0.0, "zcr": 0.0, "pitch_hz": 0.0, "energy_std": 0.0}
    rms = float(np.sqrt(np.mean(mono ** 2)))
    # Zero-crossing rate as pitch proxy: higher ZCR = higher pitch = tension
    zc = np.diff(np.sign(mono))
    zcr = float(np.sum(zc != 0) / len(mono)) * sr / 2
    # Energy variability within the block (jitter/shimmer proxy)
    frame_size = min(640, len(mono))  # 40ms frames
    n_frames = len(mono) // frame_size
    if n_frames >= 2:
        energies = [float(np.sqrt(np.mean(mono[i*frame_size:(i+1)*frame_size] ** 2)))
                    for i in range(n_frames)]
        energy_std = float(np.std(energies))
    else:
        energy_std = 0.0
    return {
        "rms": rms,
        "zcr": zcr,
        "pitch_hz": zcr,  # ZCR is a rough pitch estimate for voiced speech
        "energy_std": energy_std,
    }


def detect_emotion(prosody_history):
    """Classify emotion from a history of prosodic features.

    Uses the research-backed mappings:
    - High pitch (ZCR) + high energy + high variability = excited/happy
    - Low pitch + low energy + low variability = sad/bored
    - High pitch + high energy variability = angry/stressed
    - Low pitch + high energy = calm/focused

    Returns mood string: 'happy', 'excited', 'angry', 'sad', 'thinking', or None.
    """
    if not prosody_history or len(prosody_history) < 3:
        return None
    import numpy as np
    recent = prosody_history[-PROSODY_SAMPLES:]
    avg_rms = float(np.mean([p["rms"] for p in recent]))
    avg_zcr = float(np.mean([p["zcr"] for p in recent]))
    std_rms = float(np.std([p["rms"] for p in recent]))
    std_zcr = float(np.std([p["zcr"] for p in recent]))

    # ZCR for voiced speech: normal ~50-150 Hz, high pitch ~300+ Hz
    high_pitch = avg_zcr > 0.15  # relative to 16kHz sample rate
    low_pitch = avg_zcr < 0.06
    high_energy = avg_rms > 0.02
    low_energy = avg_rms < 0.008
    high_var = std_rms > 0.008 or std_zcr > 0.05

    if high_pitch and high_energy:
        return "excited"
    if high_pitch and high_var:
        return "angry"
    if low_pitch and low_energy:
        return "sad"
    if high_energy and low_var and not high_pitch:
        return "thinking"
    if not high_pitch and not low_pitch and avg_rms > 0.01:
        return "happy"
    return None


def find_device(prefer=("voicemeeter out b1", "voicemeeter out b2",
                        "nvidia broadcast", "ag06")):
    """Pick the streamer's mic, validating that it actually carries audio.

    Boss's rig (2026-08-04): Voicemeeter Banana + NVIDIA Broadcast + OBS holding
    every webcam. Hardware endpoints are contested -- the SAME device shows up
    under MME/WASAPI/DirectSound at different indices, and some of those return
    pure silence to a third listener (dev 22 gave noise=0.00000 while dev 7, the
    same NVIDIA Broadcast mic, read 0.032). So: prefer the Voicemeeter B-buses
    (virtual, shareable, built for exactly this), and probe before committing.

    Also rejects broken drivers: 'Voicemeeter Point' endpoints reported
    peak=512.0 (impossible for normalized float) -- garbage, not signal.
    """
    try:
        import numpy as np
        import sounddevice as sd
        devs = sd.query_devices()
    except Exception:
        return None

    def carries_audio(idx, secs=1.2):
        """True if this endpoint yields plausible, non-silent audio."""
        try:
            a = sd.rec(int(secs * SR), samplerate=SR, channels=1,
                       dtype="float32", device=idx)
            sd.wait()
            a = a.flatten()
            peak = float(np.max(np.abs(a)))
            if peak > 1.5:            # broken driver, not real signal
                return False
            return float(np.sqrt(np.mean(a ** 2))) > 0.0008
        except Exception:
            return False

    candidates = []
    for want in prefer:
        for i, d in enumerate(devs):
            if d.get("max_input_channels", 0) > 0 and want in d["name"].lower():
                candidates.append(i)
    for i in candidates:
        if carries_audio(i):
            return i
    return candidates[0] if candidates else None


class Ears(threading.Thread):
    """Continuous listener. Calls on_utterance(text) from this thread."""

    def __init__(self, on_utterance, device=None, gate=RMS_GATE):
        super().__init__(daemon=True)
        self.on_utterance = on_utterance
        self.device = device if device is not None else find_device()
        self.target_device = self.device  # user's preferred device index
        self.gate = gate
        self.running = False
        self.enabled = True      # flipped off while the cohost speaks
        self.last_error = None
        self.heard_count = 0
        self.noise_floor = 0.0
        self._model = None
        self._device_check_interval = 5.0  # re-probe device every 5s if lost
        # Real-time prosody tracking for emotion-aware reactions
        self.prosody_history = []  # rolling window of prosodic features
        self.detected_emotion = None  # last detected emotion from voice

    # -- speech-to-text ----------------------------------------------------
    def _load_model(self):
        if self._model is None:
            from faster_whisper import WhisperModel
            self._model = WhisperModel(MODEL_SIZE, device="cpu",
                                       compute_type="int8")
        return self._model

    def transcribe(self, audio):
        try:
            segs, _ = self._load_model().transcribe(audio, language="en",
                                                    vad_filter=True)
            return " ".join(s.text.strip() for s in segs).strip()
        except Exception as e:
            self.last_error = f"stt:{e}"
            return ""

    # -- capture -----------------------------------------------------------
    def run(self):
        try:
            import numpy as np
            import sounddevice as sd
        except Exception as e:
            self.last_error = f"audio-stack:{e}"
            return

        self.running = True
        buf = []
        speaking = False
        quiet_since = None
        started = 0.0

        def flush():
            nonlocal buf, speaking, quiet_since
            chunk, buf, speaking, quiet_since = buf, [], False, None
            if not chunk:
                return
            audio = np.concatenate(chunk)
            if len(audio) / SR < MIN_UTTERANCE:
                return
            text = self.transcribe(audio)
            if text:
                self.heard_count += 1
                try:
                    self.on_utterance(text)
                except Exception as e:
                    self.last_error = f"callback:{e}"

        try:
            # Outer retry loop: reconnect on device loss/hot-plug.
            while self.running:
                try:
                    with sd.InputStream(samplerate=SR, channels=1, dtype="float32",
                                        blocksize=BLOCK, device=self.device) as stream:
                        # Calibrate to the actual room: a hardcoded gate is a
                        # guess, and on 2026-08-04 a 0.010 gate sat ABOVE the
                        # boss's normal speaking level (0.0076) so the cohost
                        # heard nothing. Measure, then gate just above noise.
                        floor = []
                        for _ in range(int(CALIB_SECS * SR / BLOCK)):
                            blk, _o = stream.read(BLOCK)
                            floor.append(float(np.sqrt(np.mean(blk.reshape(-1) ** 2))))
                        noise = sorted(floor)[len(floor) // 2] if floor else 0.0
                        self.gate = max(RMS_GATE, noise * 2.5)
                        self.noise_floor = noise
                        print(f"[ears] noise={noise:.5f} gate={self.gate:.5f}", flush=True)

                        while self.running:
                            if not self.enabled:
                                buf, speaking, quiet_since = [], False, None
                                time.sleep(0.1)
                                continue
                            try:
                                block, _ = stream.read(BLOCK)
                            except Exception as sd_err:
                                # Device may have been unplugged/hot-plugged.
                                self.last_error = f"device lost: {sd_err}"
                                new_dev = find_device()
                                if new_dev is not None and new_dev != self.device:
                                    self.device = new_dev
                                    break  # exit inner loop; outer loop reconnects
                                time.sleep(0.5)
                                continue
                            mono = block.reshape(-1)
                            rms = float(np.sqrt(np.mean(mono ** 2)))
                            now = time.time()
                            # Extract prosodic features for emotion detection
                            if rms >= self.gate * 0.5:  # track during near-speech
                                prosody = extract_prosody(block)
                                self.prosody_history.append(prosody)
                                if len(self.prosody_history) > PROSODY_SAMPLES * 2:
                                    self.prosody_history.pop(0)
                                detected = detect_emotion(self.prosody_history)
                                if detected:
                                    self.detected_emotion = detected
                            if rms >= self.gate:
                                if not speaking:
                                    speaking, started = True, now
                                quiet_since = None
                                buf.append(mono.copy())
                            elif speaking:
                                buf.append(mono.copy())      # keep trailing silence
                                quiet_since = quiet_since or now
                                if now - quiet_since >= SILENCE_HOLD:
                                    flush()
                            if speaking and now - started >= MAX_UTTERANCE:
                                flush()
                except Exception as e:
                    self.last_error = f"stream:{e}"
                    if self.running:
                        time.sleep(1.0)  # backoff before retry
        finally:
            self.running = False

    def stop(self):
        self.running = False


def listen_forever(on_utterance, device=None):
    """Convenience: start the ears, return the thread."""
    ears = Ears(on_utterance, device=device)
    ears.start()
    return ears


if __name__ == "__main__":
    import sounddevice as sd
    dev = int(sys.argv[1]) if len(sys.argv) > 1 else None
    e = Ears(lambda t: print(f"  [heard] {t}", flush=True), device=dev)
    name = sd.query_devices(e.device)["name"] if e.device is not None else "default"
    print(f"ears on device {e.device} ({name}) -- talk to me, ctrl-c to stop")
    e.start()
    try:
        while True:
            time.sleep(1)
            if e.last_error:
                print("  [err]", e.last_error, flush=True)
                e.last_error = None
    except KeyboardInterrupt:
        e.stop()
        print(f"\nheard {e.heard_count} utterance(s)")

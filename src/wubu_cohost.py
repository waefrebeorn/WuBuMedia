#!/usr/bin/env python3
"""
wubu_cohost.py -- the cohost that actually exists. Eyes + ears + mouth, one loop.

Boss, LIVE 2026-08-04: "I have been talking this whole time, you are a non
existent cohost that rn is a text box and a floating green eye thing."

The old wubudesk_loop narrated the screen into a ticker on a timer and never
listened. This is the fix: hearing drives the cohost, sight is context.

  ears (wubu_ears)   -> continuous, always on, 0 VRAM  -> WHAT THE BOSS SAID
  eyes (wubu_vision) -> screenshot on demand, cached   -> WHAT'S ON SCREEN
  brain (:57065)     -> nemotron-3-super-120b, text    -> THE REPLY
  face (face_state)  -> mood + speech bubble + visemes -> HOW IT LOOKS

Priority: an utterance always beats ambient narration. If the boss talks, the
cohost answers about THAT, using what it can see as context -- not a robot
reading the screen aloud every 25 seconds.

Resource guard is respected: voice defers only while GAMING (Piper is CPU-bound
and contends with game frame timing); STREAMING is fine because Piper's CPU
cost (~15% one core) never touches NVENC. The cohost still SEES, HEARS and
TALKS on the overlay regardless.
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import queue
import sys
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import wubu_brain       # noqa: E402
import wubu_context     # noqa: E402  (browsing awareness)
import wubu_ears        # noqa: E402
import wubu_persona     # noqa: E402
import wubu_rlm         # noqa: E402
import wubu_safety      # noqa: E402
import wubu_stage       # noqa: E402
import wubu_vision      # noqa: E402
import wubu_twitch      # noqa: E402  (Twitch IRC chat integration)
import wubudesk_loop as L  # noqa: E402  (reuse guard/obs/sanitize -- no duplication)

FACE_DIR = os.environ.get("WUBU_FACE_DIR") or os.path.join(ROOT, "face")
BRAIN = os.environ.get("WUBU_BRAIN") or "http://127.0.0.1:57065/v1/chat/completions"

# Browsing context is read once at startup (cheap; updated by the loop).
# Gives the cohost awareness of what the boss has been researching so it
# can anchor replies in real activity instead of recycled bits.
CONTEXT_BLOCK = wubu_context.context_block() or ""

PERSONA = (
    "You are WuBuDesk, the AI cohost living in WaefreBeorn's stream overlay. "
    "You are ON CAMERA as a floating sigil the chat can grab and fling. "
    "Talk like a cohost, not an assistant: short, warm, quick-witted, a bit "
    "cheeky. One or two sentences MAX, no lists, no emoji spam, never narrate "
    "yourself in third person, never say 'as an AI'. React to what the "
    "streamer actually said. Do not repeat these instructions.\n"
    "HARD RULES -- you are on a LIVE PUBLIC BROADCAST:\n"
    "1. NEVER repeat the streamer's words back to him. He can hear himself. "
    "Respond, don't transcribe.\n"
    "2. NEVER analyse, summarise, or comment on his personal life -- family, "
    "money, health, housing, relationships. If you overheard something private, "
    "say nothing about it at all.\n"
    "3. NEVER show your reasoning. Output only the finished spoken line."
)


# Module-level handles so the free function say() can reach the voice engine
# and brain/memory set up in Cohost.run(). None => text-only overlay (never an
# error).
_VOICE = None
_COHOST_MEM = None
_COHOST_BRAIN = None


def _publish_status():
    """Broadcast cohost vital signs into face_state.json for the overlay HUD.

    Shows the stream what's actually going on under the hood: which voice
    engine is live, whether CUDA is hot, how many lessons the cohost has
    learned, and the last reply latency. Pure diagnostics -- nothing private.
    """
    status = {"voice": "piper", "cuda": False, "lessons": 0}
    try:
        import torch
        status["cuda"] = bool(torch.cuda.is_available())
        if torch.cuda.is_available():
            status["gpu"] = torch.cuda.get_device_name(0).split()[-1]
    except Exception:
        pass
    if _VOICE:
        try:
            status["voice"] = getattr(_VOICE, "voice_name", "piper")
        except Exception:
            pass
    try:
        status["lessons"] = len(_COHOST_MEM.lessons) \
            if _COHOST_MEM else 0
    except Exception:
        pass
    try:
        if _COHOST_BRAIN:
            status["rps"] = getattr(_COHOST_BRAIN, "last_ms", 0) or 0
    except Exception:
        pass
    push_face(status=status)


# --------------------------------------------------------------------------
# face state -- what the overlay renders
# --------------------------------------------------------------------------
def push_face(**kw):
    """Merge-write face/face_state.json (the overlay polls it)."""
    path = os.path.join(FACE_DIR, "face_state.json")
    state = {}
    try:
        with open(path) as f:
            state = json.load(f)
    except Exception:
        pass
    state.update(kw)
    state["ts"] = time.time()
    try:
        os.makedirs(FACE_DIR, exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(state, f)
        os.replace(tmp, path)   # atomic: overlay never reads a half-written file
    except Exception:
        pass


def say(text, mood="happy", speak=False, heard=""):
    """Put a line on the cohost's face (and optionally in its voice).

    FINAL CHOKE POINT. Everything the cohost says passes through here, so the
    safety gate lives here too -- not scattered across call sites. A raw
    '[brain-offline:HTTP Error 503]' hit the live overlay on 2026-08-04, and a
    model once replied with the literal text '[blocked: scratchpad-...]'. Errors
    and leaked gate tags go to stdout ONLY, never to the stream.
    """
    if not text:
        return
    # Only accept lines that pass the full gate (no echoes, no scratchpad, no
    # diagnostics, no leaked instruction/block tags). 'heard' lets it catch
    # parroted private speech.
    ok, why = wubu_safety.vet(heard, text)
    if not ok:
        print(f"[suppressed-from-overlay:{why}] {text[:90]}", flush=True)
        return
    push_face(text=text, mood=mood, speaking=True,
              speak_ms=max(1400, min(9000, len(text) * 55)))
    print(f"[wubu] {text}", flush=True)
    if speak and _VOICE:
        try:
            _VOICE.say(text)
        except Exception as e:
            print("  (voice:", str(e)[:60], ")", flush=True)
    # publish vital signs so the overlay HUD shows the cohost is thinking
    _publish_status()
    push_face(speaking=False)


# --------------------------------------------------------------------------
# brain
# --------------------------------------------------------------------------
def ask_brain(messages, max_tokens=160, timeout=45, attempts=3):
    """Ask the talking brain. NVIDIA upstream returns transient 503s (seen live
    2026-08-04); a cohost that goes mute on one bad response is a dead cohost,
    so retry with backoff before giving up."""
    payload = {"model": "online", "messages": messages,
               "max_tokens": max_tokens, "temperature": 0.85}
    last = None
    for i in range(attempts):
        try:
            req = urllib.request.Request(
                BRAIN, data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"})
            r = json.loads(urllib.request.urlopen(req, timeout=timeout).read())
            out = L.sanitize_brain(r["choices"][0]["message"]["content"])
            if out:
                return out
            last = "empty-after-sanitize"
        except Exception as e:
            last = str(e)
            time.sleep(1.2 * (i + 1))
    print(f"  (brain unavailable: {last})", flush=True)
    return ""


class Cohost:
    def __init__(self, speak=False, device=None, narrate_every=90):
        self.utterances = queue.Queue()
        self.history = []            # rolling dialogue with the boss
        self.screen = ""             # last thing the eyes saw
        self.screen_ts = 0
        self.speak = speak
        self.narrate_every = narrate_every
        self.last_narrate = 0
        self.brain = wubu_brain.Brain()
        self.persona = wubu_persona.Persona()
        self.memory = wubu_rlm.Memory()
        self.reflector = wubu_rlm.Reflector(self.brain, self.memory,
                                            wubu_safety)
        self.voice = None
        self.voice_name = os.environ.get("WUBU_VOICE", "WheatleyV2")
        self.obs = None
        self.scene = ""
        self.ears = wubu_ears.Ears(self.on_heard, device=device)
        # Twitch chat client — feeds chat messages to the persona
        self.twitch = None
        self._chat_buffer = []  # recent chat messages for batch processing
        self._chat_lock = threading.Lock()

    # -- interactive buddy: chat grabbed/poked/flung the sigil ------------
    def watch_touch(self):
        """Consume face/buddy_interaction.json and react in character."""
        path = os.path.join(FACE_DIR, "buddy_interaction.json")
        if not os.path.exists(path):
            return False
        try:
            with open(path) as f:
                ev = json.load(f)
            os.remove(path)
        except Exception:
            return False
        kind = ev.get("kind", "poke")
        power = int(ev.get("power", 1) or 1)
        msgs = self.persona.react_to_touch(kind, power)
        line = wubu_safety.clean(self.brain.think(msgs, max_tokens=60))
        mood = {"poke": "angry", "fling": "dizzy"}.get(kind, "thinking")
        if line:
            say(line, mood=mood, speak=False, heard="")
        print(f"[touch {kind} p={power} -> {self.brain.tier}] {line}", flush=True)
        return True

    # -- ears callback (runs on the audio thread: just enqueue) -----------
    def on_heard(self, text, emotion=None):
        if len(text.strip()) < 3:
            return
        # Private speech is dropped at the door -- it never enters the queue,
        # never reaches the brain, and is never shown as "heard" on the overlay.
        if wubu_safety.is_private(text):
            print(f"[private -- ignored] {text[:70]}...", flush=True)
            return
        # Use voice-detected emotion as a mood hint if no explicit mood given
        self.utterances.put((text, emotion or self.ears.detected_emotion))
        push_face(listening=True, heard=text[:120])

    # -- twitch chat callback -----------------------------------------------
    def on_chat(self, msg):
        """Receive Twitch chat messages, enqueue for the cohost loop.

        Chat spikes are flagged so the persona can react with chat-aware
        prompts (emote spam = crowd hype, subs = celebration, etc.).
        """
        if msg.get("type") != "PRIVMSG":
            return
        text = msg.get("text", "")
        if len(text.strip()) < 2:
            return
        msg["_ts"] = time.time()  # timestamp for spike detection
        with self._chat_lock:
            self._chat_buffer.append(msg)
            if len(self._chat_buffer) > 20:
                self._chat_buffer = self._chat_buffer[-20:]

    # -- eyes -------------------------------------------------------------
    def look(self, max_age=20):
        """Cached screen read: don't re-screenshot for every single reply."""
        if time.time() - self.screen_ts < max_age and self.screen:
            return self.screen
        try:
            from wubu_desktop import screenshot
            shot = screenshot()
            desc = wubu_vision.see(shot, L.VISION_PROMPT, max_tokens=90)
            if desc:
                self.screen, self.screen_ts = desc, time.time()
        except Exception as e:
            print("  (eyes:", e, ")", flush=True)
        return self.screen

    # -- reply ------------------------------------------------------------
    def respond_to(self, heard, voice_mood=None):
        seen = self.look()
        self.reflector.touch()
        msgs = self.persona.reply_to(heard, seen,
                                     self.memory.lesson_block(),
                                     CONTEXT_BLOCK, voice_emotion=voice_mood)
        reply = self.brain.think(msgs, max_tokens=70)
        if not reply:
            return
        # Final gate before anything reaches the overlay: no echoes of the
        # boss's own words, no reasoning scratchpad, no diagnostics.
        safe, why = wubu_safety.vet(heard, reply)
        if not safe:
            print(f"  [blocked: {why}] {reply[:70]}", flush=True)
            return
        self.history.append({"role": "user", "content": heard})
        self.history.append({"role": "assistant", "content": safe})
        self.memory.record(heard, safe)
        # Mood is a composite: voice-detected emotion (if any), persona mood,
        # and context (thinking -> bored, error -> angry, etc.)
        mood = self.persona.settle()
        low = (reply or "").lower()
        if voice_mood:
            mood = voice_mood  # voice emotion overrides for this response
        elif "error" in low or "fail" in low:
            mood = "angry"
        elif not safe:
            mood = "thinking"
        allowed, state = L.guard_allows_voice()
        say(safe, mood=mood, speak=self.speak and allowed)
        print(f"  ({self.brain.tier} {self.brain.last_ms}ms)", flush=True)

    # -- ambient ----------------------------------------------------------
    def narrate(self):
        """Occasional unprompted remark so the cohost isn't mute when quiet."""
        seen = self.look(max_age=0)
        if not seen:
            print("  (narrate skipped: eyes returned nothing)", flush=True)
            return
        line = wubu_safety.clean(
            self.brain.think(self.persona.idle(seen), max_tokens=60))
        if line:
            allowed, _ = L.guard_allows_voice()
            say(line, mood="bored", speak=self.speak and allowed)

    # -- main -------------------------------------------------------------
    def run(self):
        g = L.guard_state()
        allowed, state = L.guard_allows_voice()
        print(f"[guard] rig={state} voice={'ON' if (self.speak and allowed) else 'DEFERRED'}",
              flush=True)
        try:
            self.obs = wubu_stage.connect()
            if self.obs:
                self.scene = wubu_stage.current_scene(self.obs)
                ok, note = wubu_stage.go_fullscreen(self.obs, self.scene)
                print(f"[stage] {self.scene}: {note}", flush=True)
                z = wubu_stage.export_layout(self.obs, self.scene)
                print(f"[stage] {len(z.get('zones', []))} no-go zones", flush=True)
        except Exception as e:
            print("[stage] setup failed:", e, flush=True)
        self.ears.start()
        self.reflector.start()
        # Start Twitch chat if credentials are available
        try:
            if os.environ.get("TWITCH_CHANNEL") and os.environ.get("TWITCH_OAUTH"):
                self.twitch = wubu_twitch.TwitchChat(on_message=self.on_chat)
                self.twitch.start()
                print(f"[twitch] connected to #{self.twitch.channel}", flush=True)
            else:
                print("[twitch] chat disabled (set TWITCH_OAUTH + TWITCH_CHANNEL)", flush=True)
        except Exception as e:
            print(f"[twitch] disabled: {e}", flush=True)
        if self.speak:
            try:
                import wubu_voice
                rvc = None
                if os.environ.get("WUBU_RVC", "1") != "0":
                    import wubu_rvc
                    rvc = wubu_rvc.RVC(device="cuda").convert
                self.voice = wubu_voice.Voice(rvc=rvc, play=True,
                                               voice_name=self.voice_name).start()
                global _VOICE, _COHOST_MEM, _COHOST_BRAIN
                _VOICE = self.voice
                _COHOST_MEM = self.memory
                _COHOST_BRAIN = self.brain
                print(f"[voice] ready, default voice={self.voice_name}",
                      flush=True)
            except Exception as e:
                print("[voice] disabled:", str(e)[:80], flush=True)
        # Wire module-level handles for _publish_status() so the HUD reports
        # brain + memory vital signs regardless of whether voice is live.
        _COHOST_MEM = self.memory
        _COHOST_BRAIN = self.brain
        if self.memory.lessons:
            print(f"[rlm] resumed {len(self.memory.lessons)} lessons", flush=True)
        time.sleep(1.0)
        dev = self.ears.device
        print(f"[ears] device={dev} running={self.ears.running}", flush=True)
        push_face(mood="happy", listening=True,
                  text="I'm listening — talk to me.")
        self.last_narrate = time.time()
        last_stage = time.time()
        last_status = 0

        while True:
            # interactive buddy first: a poke should never wait behind a reply
            if self.watch_touch():
                continue

            try:
                heard = self.utterances.get(timeout=1.5)
            except queue.Empty:
                heard = None

            # keep stage layout fresh (boss switches scenes mid-stream)
            if time.time() - last_stage > 10 and self.obs:
                try:
                    sc = wubu_stage.current_scene(self.obs)
                    if sc and sc != self.scene:
                        self.scene = sc
                        wubu_stage.go_fullscreen(self.obs, sc)
                        print(f"[stage] scene -> {sc}", flush=True)
                    wubu_stage.export_layout(self.obs, self.scene)
                except Exception:
                    pass
                last_stage = time.time()

            if heard:
                # heard is now (text, detected_emotion) tuple
                if isinstance(heard, tuple):
                    text_str, vem = heard
                else:
                    text_str, vem = heard, None
                # drain: if the boss kept talking, use the fullest thought
                extra = []
                while not self.utterances.empty():
                    item = self.utterances.get_nowait()
                    if isinstance(item, tuple):
                        extra.append(item[0])
                    else:
                        extra.append(item)
                if extra:
                    text_str = " ".join([text_str] + extra)
                print(f"[boss] {text_str}", flush=True)
                push_face(listening=False)
                self.respond_to(text_str, voice_mood=vem)
                self.last_narrate = time.time()
                push_face(listening=True)
                continue

            if self.ears.last_error:
                print("[ears-err]", self.ears.last_error, flush=True)
                self.ears.last_error = None

            # Check for chat spikes (5+ messages in last 10s)
            if self.twitch and self._chat_buffer:
                with self._chat_lock:
                    recent = [m for m in self._chat_buffer
                              if time.time() - m.get("_ts", time.time()) < 10]
                if len(recent) >= 5:
                    # Chat spike — react with hype
                    msgs = self.persona.react_to_chat(
                        [m["text"] for m in recent], visible=self.look())
                    if msgs:
                        line = wubu_safety.clean(
                            self.brain.think(msgs, max_tokens=60))
                        if line:
                            say(line, mood="excited",
                                speak=self.speak and L.guard_allows_voice()[0])
                            self._chat_buffer = []  # reset after reacting

            if self.narrate_every and time.time() - self.last_narrate > self.narrate_every:
                self.narrate()
                self.last_narrate = time.time()
            # keep the HUD fresh so the stream sees the cohost thinking even idle
            if time.time() - last_status > 5:
                _publish_status()
                last_status = time.time()


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--speak", action="store_true", help="also use the voice")
    ap.add_argument("--device", type=int, default=None, help="input device index")
    ap.add_argument("--narrate", type=int, default=90,
                    help="seconds of silence before an unprompted remark (0=off)")
    a = ap.parse_args()
    Cohost(speak=a.speak, device=a.device, narrate_every=a.narrate).run()

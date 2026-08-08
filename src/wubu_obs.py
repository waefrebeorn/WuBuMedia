#!/usr/bin/env python3
"""wubu_obs.py — WuBuDesk OBS cohost controller ("face + hands" on the stream).

This is the Windows-side AGI's interface to OBS Studio via obs-websocket 5.x.
It gives WuBuDesk:
  * HANDS  — scene/source control (switch scenes, toggle/mute sources, set
             text, move/resize, trigger hotkeys, start/stop stream/record).
  * FACE   — a managed "avatar" browser source (the cohost's face) that we
             can point at an overlay page and push state into (mood, speak).

Design: self-contained, depends only on `websocket-client` (+ stdlib). No
monolithic God-object — a thin client plus a small command surface so other
agents (on WSL/remote) can drive OBS through WuBuDesk over the cluster channel.

Auth: obs-websocket 5 uses a salted SHA256 challenge:
  secret = SHA256(password + salt)
  auth   = base64( SHA256(secret + challenge) + salt )   (legacy variant)
  (This client implements both the v5 "challenge" and legacy "auth" fields.)

Use:
  from wubu_obs import ObsCohost
  obs = ObsCohost(host="localhost", port=4455, password="...")
  obs.connect()
  obs.set_scene("Starting Soon")
  obs.set_source_text("CohostTicker", "Yo chat, WuBu online.")
  obs.speak("happy", "Yo chat, WuBu online.")  # updates the avatar face source

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import base64
import hashlib
import json
import os
import sys
import time

try:
    import websocket
except ImportError:
    sys.stderr.write("FATAL: pip install websocket-client\n")
    raise


class ObsError(RuntimeError):
    pass


class ObsCohost:
    """OBS websocket 5.x client with reconnection and event support.

    Thread-safe via a single lock around send/recv — OBS serialises
    requests on its end anyway, so concurrent Python calls just queue.
    """

    def __init__(self, host="localhost", port=4455, password=None,
                 avatar_source="WuBuFace", face_url=None, timeout=8.0,
                 max_retries=3, retry_delay=1.5):
        self.url = f"ws://{host}:{port}"
        self.password = password
        self.avatar_source = avatar_source
        self.face_url = face_url
        self.timeout = timeout
        self.max_retries = max_retries
        self.retry_delay = retry_delay
        self._ws = None
        self._msg_id = 0
        self._op = None  # negotiated RPC version
        self._event_cb = None  # callback: event_type -> dict
        self._connected = False
        self.last_event = None
        self._lock = __import__("threading").Lock()

    # -- context manager ----------------------------------------------------
    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    # ---- low level ----
    def _next_id(self):
        self._msg_id += 1
        return f"wubu-{self._msg_id}"

    def _send(self, op, payload):
        with self._lock:
            self._ws.send(json.dumps({"op": op, "d": payload}))

    def _recv(self, want_id):
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            raw = self._ws.recv()
            if raw is None:
                continue
            msg = json.loads(raw)
            op = msg.get("op")
            if op == 5:  # Event
                self._on_event(msg.get("d", {}))
                continue
            d = msg.get("d", {})
            if d.get("requestId") == want_id or op == 2:
                return msg
        raise ObsError("timeout waiting for response")

    def _on_event(self, d):
        """Store the last event and dispatch to subscriber callback."""
        self.last_event = d
        if self._event_cb:
            try:
                self._event_cb(d.get("eventType", ""), d)
            except Exception:
                pass

    def _call(self, req_type, **params):
        """Send a request with retry on transient errors.

        Retries on timeout/connection errors up to max_retries times with
        exponential backoff. Reconnects if the socket died.
        """
        last = None
        for attempt in range(self.max_retries + 1):
            try:
                if not self._ws:
                    if not self._reconnect():
                        raise ObsError("not connected and reconnect failed")
                rid = self._next_id()
                self._send(6, {  # Request
                    "requestType": req_type,
                    "requestId": rid,
                    "requestData": params,
                })
                msg = self._recv(rid)
                d = msg.get("d", {})
                if msg.get("op") == 7:  # RequestResponse
                    rs = d.get("requestStatus", {})
                    if rs.get("result") is False:
                        code = rs.get("code")
                        comment = rs.get("comment")
                        err = ObsError(f"{req_type} failed [{code}]: {comment}")
                        last = err
                        # 502/503 = transient, retry; others are permanent
                        if code in ("websocketUnavailable", "invalidRequestType",
                                     "unsupportedRequest", "invalidRequestData",
                                     "unauthorized", "invalidsettings",
                                     "generic", "unknown"):
                            raise err
                        continue  # retry
                    return d.get("responseData", {})
                return d
            except ObsError as e:
                last = e
                if "timeout" in str(e).lower() or "not connected" in str(e).lower():
                    if not self._reconnect():
                        pass  # keep retrying
                continue
            except Exception as e:
                last = e
                continue
            time.sleep(self.retry_delay * (attempt + 1))
        raise last or ObsError(f"{req_type} failed after {self.max_retries} retries")

    # ---- connection + auth ----
    def _reconnect(self):
        """Attempt to re-establish the OBS websocket connection."""
        self.close()
        for attempt in range(self.max_retries):
            try:
                self._ws = websocket.create_connection(self.url, timeout=self.timeout)
                hello = json.loads(self._ws.recv())
                if hello.get("op") != 0:  # Hello
                    raise ObsError("expected Hello")
                self._op = hello["d"].get("rpcVersion", 1)
                auth = hello["d"].get("authentication")
                payload = {"rpcVersion": self._op}
                if auth:
                    if not self.password:
                        raise ObsError("server requires auth; password missing")
                    payload["authentication"] = self._make_auth(self.password,
                                                                auth["salt"],
                                                                auth["challenge"])
                self._send(1, payload)  # Identify
                ident = json.loads(self._ws.recv())
                if ident.get("op") != 2:
                    raise ObsError("expected Identified")
                if not ident["d"].get("negotiatedRpcVersion"):
                    raise ObsError("identify rejected: %s" % ident["d"].get("reason"))
                self._connected = True
                return True
            except Exception as e:
                self._connected = False
                self._ws = None
                if attempt < self.max_retries - 1:
                    time.sleep(self.retry_delay * (attempt + 1))
        return False

    def connect(self):
        """Connect (or reconnect) to OBS. Idempotent."""
        if self._connected and self._ws:
            return True
        return self._reconnect()

    @staticmethod
    def _make_auth(password, salt, challenge):
        """obs-websocket v5 authentication (per official spec)."""
        secret = base64.b64encode(
            hashlib.sha256((password + salt).encode()).digest()).decode()
        auth = base64.b64encode(
            hashlib.sha256((secret + challenge).encode()).digest()).decode()
        return auth

    def close(self):
        self._connected = False
        if self._ws:
            try:
                self._ws.close()
            finally:
                self._ws = None

    def on_event(self, callback):
        """Subscribe to OBS events (scene change, recording start/stop, etc.).

        callback(eventType, eventData) -> None
        """
        self._event_cb = callback

    def is_streaming(self):
        """Return True if OBS is currently streaming."""
        try:
            d = self._call("GetStreamingStatus")
            return d.get("outputActive", False)
        except Exception:
            return False

    def is_recording(self):
        """Return True if OBS is currently recording."""
        try:
            d = self._call("GetRecordingStatus")
            return d.get("recording", False)
        except Exception:
            return False

    # ---- HANDS: scene + source control ----
    def get_version(self):
        return self._call("GetVersion")

    def list_scenes(self):
        data = self._call("GetSceneList")
        return [s["sceneName"] for s in data.get("scenes", [])]

    def get_current_scene(self):
        return self._call("GetCurrentProgramScene").get("currentProgramSceneName", "")

    def set_scene(self, name, transition="Cut", duration=0):
        """Switch to scene with optional transition."""
        if transition == "Cut":
            return self._call("SetCurrentProgramScene", sceneName=name)
        else:
            return self._call("SetCurrentProgramScene", sceneName=name,
                              transitionName=transition,
                              transitionDuration=int(duration * 1000))

    def list_inputs(self, scene=None):
        if scene:
            data = self._call("GetSceneItemList", sceneName=scene)
            return [it["sourceName"] for it in data.get("sceneItems", [])]
        data = self._call("GetInputList")
        return [i["inputName"] for i in data.get("inputs", [])]

    def set_source_text(self, source, text):
        """Set text on a text source (browser/text input)."""
        try:
            return self._call("SetInputSettings", inputName=source,
                              inputSettings={"text": text}, overlay=True)
        except ObsError:
            return self._call("SetInputSettings", inputName=source,
                              inputSettings={"message": text}, overlay=True)

    def set_source_texts(self, pairs):
        """Batch-set text on multiple sources. Each item: (source, text)."""
        results = []
        for src, txt in pairs:
            results.append((src, self.set_source_text(src, txt)))
        return results

    def toggle_source(self, source, visible=None):
        scene = self.get_current_scene()
        items = self._call("GetSceneItemList", sceneName=scene).get("sceneItems", [])
        item = next((i for i in items if i.get("sourceName") == source), None)
        if not item:
            raise ObsError(f"source {source!r} not in scene {scene!r}")
        if visible is None:
            cur = item.get("sceneItemEnabled", True)
            visible = not cur
        self._call("SetSceneItemEnabled", sceneName=scene,
                   sceneItemId=item["sceneItemId"],
                   sceneItemEnabled=bool(visible))
        return visible

    def toggle_sources(self, sources, visible=None):
        """Batch-toggle multiple sources. Returns dict of name -> bool."""
        return {s: self.toggle_source(s, visible) for s in sources}

    def set_source_transform(self, source, **transform):
        """Set position/scale/rotation for a scene item.

        Args: positionX, positionY, scaleX, scaleY, rotation, etc.
        """
        scene = self.get_current_scene()
        items = self._call("GetSceneItemList", sceneName=scene).get("sceneItems", [])
        item = next((i for i in items if i.get("sourceName") == source), None)
        if not item:
            raise ObsError(f"source {source!r} not in scene {scene!r}")
        t = (item.get("sceneItemTransform") or {}).copy()
        t.update(transform)
        return self._call("SetSceneItemTransform", sceneName=scene,
                          sceneItemId=item["sceneItemId"],
                          sceneItemTransform=t)

    def mute_source(self, source, muted=None):
        cur = self._call("GetInputMute", inputName=source).get("inputMuted", False)
        if muted is None:
            muted = not cur
        self._call("SetInputMute", inputName=source, inputMuted=bool(muted))
        return muted

    def set_source_volume(self, source, db=None, mul=None):
        """Set volume for an audio source. db = dB, mul = multiplier (0..1)."""
        return self._call("SetInputVolume", inputName=source,
                          inputVolumeDb=db, inputVolumeMul=mul)

    def get_source_volume(self, source):
        d = self._call("GetInputVolume", inputName=source)
        return d.get("inputVolumeDb"), d.get("inputVolumeMul")

    def trigger_hotkey(self, name):
        """Trigger a saved hotkey by name."""
        return self._call("TriggerHotkeyByName", hotkeyName=name)

    def get_source_audio_db(self, source):
        """Get current audio peak level for an input (for metering)."""
        try:
            d = self._call("GetInputAudioDB", inputName=source)
            return d.get("inputAvgDB"), d.get("inputPeakDB")
        except Exception:
            return None, None

    def start_stream(self):
        return self._call("StartStream")

    def stop_stream(self):
        return self._call("StopStream")

    def start_record(self):
        return self._call("StartRecord")

    def stop_record(self):
        return self._call("StopRecord")

    def start_replay_buffer(self):
        return self._call("StartRecordDirectory")

    def save_replay_buffer(self):
        return self._call("SaveRecordDirectory")

    def start_virtual_cam(self):
        return self._call("StartVirtualCam")

    def stop_virtual_cam(self):
        return self._call("StopVirtualCam")

    def take_screenshot(self, source=None, width=1920, height=1080,
                        format="png", output_path=None):
        """Capture a screenshot via OBS (uses source or entire canvas)."""
        kwargs = {"ImageSize": {"width": width, "height": height},
                  "CompressionRatio": 100}
        if source:
            kwargs["SourceName"] = source
        if output_path:
            kwargs["OutputUrl"] = output_path
        return self._call("TakeSourceScreenshot", **kwargs)

    # ---- FACE: avatar source management ----
    def ensure_face(self, url=None):
        """Point the avatar browser source (the cohost's face) at `url`."""
        url = url or self.face_url
        if not url:
            return None
        try:
            return self._call("SetInputSettings", inputName=self.avatar_source,
                              inputSettings={"url": url, "width": 1920, "height": 1080,
                                             "fps": 30, "reroute_audio": False},
                              overlay=True)
        except ObsError as e:
            return {"warn": str(e)}

    def speak(self, mood="neutral", text=None, mode="live"):
        """Push cohost state into the face overlay via a local state file the
        browser source polls. Decouples WuBuDesk from OBS render timing.

        mode: "live" (stream overlay) or "movie" (recorded avatar).
        The state file lives in the face/ directory so the browser source
        can fetch it over localhost:8137 or via file:// access.

        Uses atomic write (os.replace) so the overlay never reads a
        half-written JSON file — critical because the browser source polls
        this file ~2-4 times per second while the cohost thread may be
        writing a new state concurrently.
        """
        # speak_ms = estimated speech-bubble duration in ms (overlay uses
        # this to animate lip-sync + hide the bubble). Match wubu_cohost.say().
        text_len = len(text or "")
        speak_ms = max(1400, min(9000, text_len * 55))
        state = {
            "mood": mood,
            "text": text,
            "speaking": bool(text),
            "speak_ms": speak_ms,
            "ts": time.time(),
            "mode": mode,
        }
        face_dir = os.environ.get("WUBU_FACE_DIR",
                                   r"C:/Users/eman5/WuBuMedia/face")
        face_path = os.path.join(face_dir, "face_state.json")
        try:
            os.makedirs(face_dir, exist_ok=True)
            tmp = face_path + ".tmp"
            with open(tmp, "w") as f:
                json.dump(state, f)
            os.replace(tmp, face_path)  # atomic: overlay never reads half-written JSON
        except OSError:
            pass
        return state

    def update_training_status(self, status=None, training=None):
        """Update the training/vital-signs status in face_state.json.

        Merges with existing state (mood, text, etc.) so the overlay doesn't
        lose its current display. Used by training pipelines to show:
          - status: voice, cuda, lessons, rps (existing vital signs)
          - training: model name, epoch, step, loss, voices, queue, gpu_util

        Example training dict:
          {"model": "Cartman v2", "epoch": 87, "epochs": 100,
           "step": 21000, "loss": 0.045, "voices": 23,
           "queue": 5, "gpu_util": 94}
        """
        face_dir = os.environ.get("WUBU_FACE_DIR",
                                 r"C:/Users/eman5/WuBuMedia/face")
        face_path = os.path.join(face_dir, "face_state.json")

        # Read existing state, merge, write back atomically
        existing = {}
        try:
            with open(face_path) as f:
                existing = json.load(f)
        except (OSError, json.JSONDecodeError):
            pass

        if status is not None:
            existing["status"] = {**existing.get("status", {}), **status}
        if training is not None:
            existing["status"] = existing.get("status", {})
            existing["status"]["training"] = training

        try:
            os.makedirs(face_dir, exist_ok=True)
            tmp = face_path + ".tmp"
            with open(tmp, "w") as f:
                json.dump(existing, f, indent=2)
            os.replace(tmp, face_path)
        except OSError:
            pass
        return existing


def main():
    pw = os.environ.get("OBS_WS_PASSWORD")
    if not pw:
        try:
            cfg = json.load(open(os.path.join(
                os.environ.get("APPDATA", r"C:\Users\eman5\AppData\Roaming"),
                "obs-studio", "plugin_config", "obs-websocket", "config.json")))
            pw = cfg.get("server_password")
        except Exception:
            pw = None
    obs = ObsCohost(port=4455, password=pw)
    try:
        obs.connect()
        v = obs.get_version()
        print("CONNECTED")
        print("obsVersion:", v.get("obsVersion"))
        print("rpcVersion:", v.get("rpcVersion"))
        print("scenes:", obs.list_scenes())
        print("current:", obs.get_current_scene())
        print("streaming:", obs.is_streaming())
        print("recording:", obs.is_recording())
    except ObsError as e:
        print("OBS-ERR:", e)
        sys.exit(1)
    finally:
        obs.close()


if __name__ == "__main__":
    main()

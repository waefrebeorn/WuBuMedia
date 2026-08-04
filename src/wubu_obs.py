#!/usr/bin/env python3
"""
wubu_obs.py — WuBuDesk OBS cohost controller ("face + hands" on the stream).

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
  obs.speak("happy")   # updates the avatar face source
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
    def __init__(self, host="localhost", port=4455, password=None,
                 avatar_source="WuBuFace", face_url=None, timeout=8.0):
        self.url = f"ws://{host}:{port}"
        self.password = password
        self.avatar_source = avatar_source
        self.face_url = face_url
        self.timeout = timeout
        self._ws = None
        self._msg_id = 0
        self._op = None  # negotiated RPC version

    # ---- low level ----
    def _next_id(self):
        self._msg_id += 1
        return f"wubu-{self._msg_id}"

    def _send(self, op, payload):
        msg = {"op": op, "d": payload}
        self._ws.send(json.dumps(msg))

    def _recv(self, want_id):
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            raw = self._ws.recv()
            if raw is None:
                continue
            msg = json.loads(raw)
            if msg.get("op") == 5:  # Event
                continue
            d = msg.get("d", {})
            if d.get("requestId") == want_id or msg.get("op") == 2:
                return msg
        raise ObsError("timeout waiting for response")

    def _call(self, req_type, **params):
        if not self._ws:
            raise ObsError("not connected")
        rid = self._next_id()
        self._send(6, {  # Request
            "requestType": req_type,
            "requestId": rid,
            "requestData": params,
        })
        msg = self._recv(rid)
        d = msg.get("d", {})
        if msg.get("op") == 7:  # RequestResponse
            if d.get("requestStatus", {}).get("result") is False:
                code = d["requestStatus"].get("code")
                comment = d["requestStatus"].get("comment")
                raise ObsError(f"{req_type} failed [{code}]: {comment}")
            return d.get("responseData", {})
        return d

    # ---- connection + auth ----
    def connect(self):
        self._ws = websocket.create_connection(self.url, timeout=self.timeout)
        hello = json.loads(self._ws.recv())
        if hello.get("op") != 0:  # Hello
            raise ObsError("expected Hello, got %r" % hello)
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
            raise ObsError("expected Identified, got %r" % ident)
        if not ident["d"].get("negotiatedRpcVersion"):
            raise ObsError("identify rejected: %s" % ident["d"].get("reason"))
        return True

    @staticmethod
    def _make_auth(password, salt, challenge):
        # obs-websocket v5 authentication (per official spec):
        #   secret = base64(sha256(password + salt))
        #   auth   = base64(sha256(secret + challenge))
        secret = base64.b64encode(
            hashlib.sha256((password + salt).encode()).digest()).decode()
        auth = base64.b64encode(
            hashlib.sha256((secret + challenge).encode()).digest()).decode()
        return auth

    def close(self):
        if self._ws:
            try:
                self._ws.close()
            finally:
                self._ws = None

    # ---- HANDS: scene + source control ----
    def get_version(self):
        return self._call("GetVersion")

    def list_scenes(self):
        data = self._call("GetSceneList")
        return [s["sceneName"] for s in data.get("scenes", [])]

    def set_scene(self, name):
        return self._call("SetCurrentProgramScene", sceneName=name)

    def get_sources(self, scene=None):
        if scene:
            data = self._call("GetSceneItemList", sceneName=scene)
            return [it["sourceName"] for it in data.get("sceneItems", [])]
        data = self._call("GetInputList")
        return [i["inputName"] for i in data.get("inputs", [])]

    def set_source_text(self, source, text):
        """Set text on a text source (browser/text input)."""
        # Text sources expose text via settings; try common field names.
        try:
            return self._call("SetInputSettings", inputName=source,
                              inputSettings={"text": text}, overlay=True)
        except ObsError:
            return self._call("SetInputSettings", inputName=source,
                              inputSettings={"message": text}, overlay=True)

    def toggle_source(self, source, visible=None):
        # find scene item id via current scene
        scene = self._call("GetCurrentProgramScene")["currentProgramSceneName"]
        items = self._call("GetSceneItemList", sceneName=scene)["sceneItems"]
        item = next((i for i in items if i["sourceName"] == source), None)
        if not item:
            raise ObsError(f"source {source!r} not in scene {scene!r}")
        if visible is None:
            cur = item.get("sceneItemEnabled", True)
            visible = not cur
        self._call("SetSceneItemEnabled", sceneName=scene,
                   sceneItemId=item["sceneItemId"], sceneItemEnabled=bool(visible))
        return visible

    def mute_source(self, source, muted=None):
        cur = self._call("GetInputMute", inputName=source).get("inputMuted", False)
        if muted is None:
            muted = not cur
        self._call("SetInputMute", inputName=source, inputMuted=bool(muted))
        return muted

    def start_stream(self):
        return self._call("StartStream")

    def stop_stream(self):
        return self._call("StopStream")

    def start_record(self):
        return self._call("StartRecord")

    def stop_record(self):
        return self._call("StopRecord")

    # ---- FACE: avatar source management ----
    def ensure_face(self, url=None):
        """Point the avatar browser source (the cohost's face) at `url`."""
        url = url or self.face_url
        if not url:
            return None
        try:
            return self._call("SetInputSettings", inputName=self.avatar_source,
                              inputSettings={"url": url}, overlay=True)
        except ObsError as e:
            # source may not exist yet; that's fine — caller creates it
            return {"warn": str(e)}

    def speak(self, mood="neutral", text=None):
        """Push cohost state into the face overlay via a local state file the
        browser source polls. Decouples WuBuDesk from OBS render timing.

        The overlay (face/index.html) fetches `face_state.json` RELATIVE to its
        own location, so we write next to it by default. Override with the
        WUBU_FACE_DIR env var (e.g. if OBS serves the page from elsewhere)."""
        face_dir = os.environ.get("WUBU_FACE_DIR") or os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "face")
        os.makedirs(face_dir, exist_ok=True)
        state = {"mood": mood, "text": text, "ts": time.time()}
        try:
            with open(os.path.join(face_dir, "face_state.json"), "w") as f:
                json.dump(state, f)
        except OSError:
            pass
        return state


def main():
    import os
    pw = os.environ.get("OBS_WS_PASSWORD")
    if not pw:
        # read the LIVE password from OBS's websocket config (it regenerates on migration)
        import json as _json
        try:
            _cfg = _json.load(open(os.path.join(
                os.environ.get("APPDATA", r"C:\Users\eman5\AppData\Roaming"),
                "obs-studio", "plugin_config", "obs-websocket", "config.json")))
            pw = _cfg.get("server_password")
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
        print("current:", obs._call("GetCurrentProgramScene").get("currentProgramSceneName"))
    except ObsError as e:
        print("OBS-ERR:", e)
        sys.exit(1)
    finally:
        obs.close()


if __name__ == "__main__":
    main()

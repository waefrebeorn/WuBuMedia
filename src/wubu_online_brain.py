#!/usr/bin/env python3
"""wubu_online_brain.py -- WuBuDesk cohost ONLINE brain (OpenAI-compatible proxy).

Sits locally as an OpenAI-compatible /v1/chat/completions server so the existing
cohost loop (wubudesk_loop.py -> BRAIN env) can talk to a REAL online model
without changing the loop. Routes to the free tiers the boss authorized:
  * NVIDIA nemotron-3-super-120b-a12b  (integrate.api.nvidia.com, NVIDIA_API_KEY)
  * Nous portal free API               (NOUS-managed key, provider=nous)

Why: the local :57064 was serving a tiny ollama model. For the live stream the
cohost needs a real brain. We properly make it: a tiny self-hosted proxy, no
third-party cohost service. C11-port spirit -> keep it small and self-contained.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os, json, urllib.request, threading
from http.server import BaseHTTPRequestHandler, HTTPServer

ENV = r"C:\Users\eman5\AppData\Local\hermes\profiles\wubudesk\.env"

def _load_env():
    d = {}
    try:
        for line in open(ENV):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            d[k.strip()] = v.strip()
    except Exception:
        pass
    return d

ENVV = _load_env()
NVIDIA_KEY = ENVV.get("NVIDIA_API_KEY", "")
# Nous key (if present in the same .env); Nous portal is also the agent default.
NOUS_KEY = ENVV.get("NOUS_API_KEY", ENVV.get("OPENROUTER_API_KEY", ""))

PORT = int(os.environ.get("WUBU_BRAIN_PORT", "57065"))
NVIDIA_URL = "https://integrate.api.nvidia.com/v1/chat/completions"
NVIDIA_MODEL = "nvidia/nemotron-3-super-120b-a12b"

# Nous free portal: the agent already routes via provider=nous / tencent/hy3:free.
# We expose it here too so the cohost has a second online path.
NOUS_URL = "https://inference.nousresearch.com/v1/chat/completions"


def _post(url, headers, payload):
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode()


def chat(messages, temperature=0.4, max_tokens=180):
    # NVIDIA nemotron free endpoint is TEXT-ONLY. The cohost loop may send
    # vision messages (image_url). Strip image parts so the text model gets
    # clean text (the loop's local :57064 brain handles true multimodal).
    clean = []
    for m in messages:
        content = m.get("content")
        if isinstance(content, list):
            text = " ".join(p.get("text", "") for p in content
                            if isinstance(p, dict) and p.get("type") == "text")
            clean.append({"role": m.get("role", "user"), "content": text})
        else:
            clean.append({"role": m.get("role", "user"), "content": content or ""})
    payload = {"model": NVIDIA_MODEL, "messages": clean,
               "temperature": temperature, "max_tokens": max_tokens, "stream": False}
    hdrs = {"Authorization": f"Bearer {NVIDIA_KEY}",
            "Content-Type": "application/json"}
    try:
        raw = _post(NVIDIA_URL, hdrs, payload)
        return json.loads(raw)["choices"][0]["message"]["content"]
    except Exception as e:
        if NOUS_KEY:
            try:
                p2 = dict(payload); p2["model"] = "tencent/hy3:free"
                h2 = {"Authorization": f"Bearer {NOUS_KEY}",
                      "Content-Type": "application/json"}
                raw = _post(NOUS_URL, h2, p2)
                return json.loads(raw)["choices"][0]["message"]["content"]
            except Exception:
                pass
        return f"[brain-offline:{e}]"


class H(BaseHTTPRequestHandler):
    def _send(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_POST(self):
        if self.path.rstrip("/") not in ("/v1/chat/completions", "/chat/completions"):
            self._send({"error": "not found"}, 404); return
        try:
            ln = int(self.headers.get("Content-Length", 0))
            req = json.loads(self.rfile.read(ln))
        except Exception as e:
            self._send({"error": str(e)}, 400); return
        msgs = req.get("messages", [])
        temp = float(req.get("temperature", 0.4))
        mt = int(req.get("max_tokens", 180))
        text = chat(msgs, temperature=temp, max_tokens=mt)
        self._send({
            "id": "wubu-online", "object": "chat.completion", "model": NVIDIA_MODEL,
            "choices": [{"index": 0, "finish_reason": "stop",
                         "message": {"role": "assistant", "content": text}}],
            "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0}
        })

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    srv = HTTPServer(("127.0.0.1", PORT), H)
    print(f"[wubu_online_brain] listening on 127.0.0.1:{PORT} (NVIDIA free + Nous fallback)")
    srv.serve_forever()

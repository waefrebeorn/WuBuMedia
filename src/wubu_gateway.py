#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_gateway.py — Unified REST API + WebSocket gateway for the WuBuDesk AGI.

Phase 10: API Gateway for AGI Integration.

A single entry point that merges all cohost subsystems into one cohesive
API surface. Built with the Python stdlib http.server for zero-dependency
deployment, with async support via threading.

Research:
  * FastAPI architecture: async endpoints, OpenAPI docs, type validation
    (https://fastapi.tiangolo.com/)
  * But we use stdlib to avoid dependency conflicts in the OBS browser source
    environment
  * REST API best practices: JSON content negotiation, error responses,
    pagination, rate limiting

Endpoints:
  GET  /api/health         — System health (voice, GPU, lessons, rps)
  GET  /api/stats          — System stats (CPU, GPU, memory)
  GET  /api/cookies?domain=github.com — Browser cookie extraction
  GET  /api/hotkeys        — Hotkey bindings + recent events
  POST /api/hotkey         — Trigger a hotkey action
  POST /api/say            — Make the cohost say something (body: text, mood)
  POST /api/poke           — Simulate a poke (body: power)
  POST /api/fling          — Simulate a fling (body: power)
  GET  /api/wiki/search?q=... — Search knowledge base
  GET  /api/wiki/fact?key=... — Get a structured fact
  GET  /api/wiki/article/<slug> — Get a wiki article
  POST /api/wiki/upsert         — Add a wiki article
  GET  /api/capture/devices     — List UVC capture devices
  POST /api/capture/optimize    — Optimize capture card
  GET  /ws                     — WebSocket: real-time face state, hotkeys

Auth: Bearer token (WUBU_GATEWAY_TOKEN env var)

Usage:
  python src/wubu_gateway.py [--port 8163]

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import json
import time
import threading
import asyncio
import hmac
import hashlib
import sqlite3
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

PORT = int(os.environ.get("WUBU_GATEWAY_PORT", "8163"))
TOKEN = os.environ.get("WUBU_GATEWAY_TOKEN", "")
WIKI_DB = os.path.join(ROOT, "knowledge", "wiki.db")
FACE_DIR = os.environ.get("WUBU_FACE_DIR", os.path.join(ROOT, "face"))

# WebSocket clients (for real-time push)
_ws_clients = []
_ws_lock = threading.Lock()


def _now():
    return time.time()


def _check_auth(headers):
    """Verify Bearer token."""
    if not TOKEN:
        return True  # dev mode
    auth = headers.get("Authorization", "")
    if auth.startswith("Bearer "):
        token = auth[7:]
        return hmac.compare_digest(token, TOKEN)
    return False


def _send_json(handler, data, status=200):
    body = json.dumps(data, default=str).encode()
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Access-Control-Allow-Origin", "*")
    handler.send_header("Access-Control-Allow-Headers", "Authorization,Content-Type")
    handler.end_headers()
    handler.wfile.write(body)


def _send_error(handler, code, msg):
    _send_json(handler, {"error": msg, "code": code}, status=code)


def _handle_api_get(handler):
    """Handle GET /api/* routes."""
    parsed = urlparse(handler.path)
    path = parsed.path
    qs = parse_qs(parsed.query)

    if path == "/api/health":
        # Face state health
        face_path = os.path.join(FACE_DIR, "face_state.json")
        try:
            with open(face_path) as f:
                state = json.load(f)
            face_age = _now() - os.stat(face_path).st_mtime
        except Exception:
            state = {}
            face_age = None

        # Wiki stats
        wiki_stats = {"articles": 0, "facts": 0}
        try:
            conn = sqlite3.connect(WIKI_DB)
            wiki_stats["articles"] = conn.execute("SELECT COUNT(*) FROM articles").fetchone()[0]
            wiki_stats["facts"] = conn.execute("SELECT COUNT(*) FROM facts").fetchone()[0]
            conn.close()
        except Exception:
            pass

        # System stats
        try:
            from wubu_agent import stats
            sys_stats = stats()
        except Exception as e:
            sys_stats = {"error": str(e)}

        _send_json(handler, {
            "ok": True,
            "timestamp": _now(),
            "face_state": state,
            "face_age_seconds": face_age,
            "wiki": wiki_stats,
            "system": sys_stats,
        })

    elif path == "/api/stats":
        try:
            from wubu_agent import stats
            _send_json(handler, stats())
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    elif path == "/api/cookies":
        domain = qs.get("domain", [None])[0]
        try:
            from wubu_bridge import read_browser_cookies
            cookies = read_browser_cookies(domain=domain)
            _send_json(handler, {"count": len(cookies), "cookies": cookies[:50]})
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    elif path == "/api/hotkeys":
        from wubu_bridge import HOTKEYS
        with _ws_lock:
            events = list(reversed(_hotkey_events[-20:]))
        _send_json(handler, {
            "bindings": [h[0] for h in HOTKEYS],
            "recent_events": events,
        })

    elif path == "/api/wiki/search":
        query = qs.get("q", [""])[0]
        tag = qs.get("tag", [None])[0]
        limit = int(qs.get("limit", ["20"])[0])
        try:
            from wubu_wiki import Wiki
            wiki = Wiki()
            results = wiki.search(query, limit=limit, tag=tag)
            _send_json(handler, {"query": query, "results": results,
                                 "count": len(results)})
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    elif path == "/api/wiki/fact":
        key = qs.get("key", [""])[0]
        try:
            from wubu_wiki import Wiki
            wiki = Wiki()
            fact = wiki.get_fact(key)
            _send_json(handler, fact or {"error": "not found"},
                       status=200 if fact else 404)
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    elif path.startswith("/api/wiki/article/"):
        slug = path.split("/api/wiki/article/")[1]
        try:
            from wubu_wiki import Wiki
            wiki = Wiki()
            article = wiki.get(slug)
            _send_json(handler, article or {"error": "not found"},
                       status=200 if article else 404)
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    elif path == "/api/capture/devices":
        try:
            from wubu_capture_direct import discover_devices
            devices = discover_devices()
            _send_json(handler, {"devices": devices, "count": len(devices)})
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    elif path == "/api/wiki/stats":
        try:
            from wubu_wiki import Wiki
            wiki = Wiki()
            _send_json(handler, wiki.stats())
        except Exception as e:
            _send_json(handler, {"error": str(e)}, status=500)

    else:
        _send_error(handler, 404, "not found")


def _handle_api_post(handler, body):
    """Handle POST /api/* routes."""
    parsed = urlparse(handler.path)
    path = parsed.path

    if path == "/api/say":
        text = body.get("text", "")
        mood = body.get("mood", "happy")
        try:
            from wubu_cohost import push_face
            push_face(text=text, mood=mood, speaking=True,
                      speak_ms=max(1400, min(9000, len(text) * 55)))
            _send_json(handler, {"ok": True, "text": text, "mood": mood})
        except Exception as e:
            _send_error(handler, 500, str(e))

    elif path == "/api/poke":
        power = int(body.get("power", 1))
        try:
            from wubu_cohost import push_face
            push_face(mood="angry")
            _send_json(handler, {"ok": True, "action": "poke", "power": power})
        except Exception as e:
            _send_error(handler, 500, str(e))

    elif path == "/api/fling":
        power = int(body.get("power", 14))
        try:
            from wubu_cohost import push_face
            push_face(mood="dizzy")
            _send_json(handler, {"ok": True, "action": "fling", "power": power})
        except Exception as e:
            _send_error(handler, 500, str(e))

    elif path == "/api/hotkey":
        action = body.get("action", "")
        _send_json(handler, {"ok": True, "action": action})

    elif path == "/api/capture/optimize":
        fmt = body.get("format", "auto")
        try:
            from wubu_capture_direct import CaptureOptimizer
            opt = CaptureOptimizer()
            result = {"optimal": opt.OPTIMAL_SETTINGS, "requested": fmt}
            _send_json(handler, result)
        except Exception as e:
            _send_error(handler, 500, str(e))

    elif path == "/api/wiki/upsert":
        slug = body.get("slug", "")
        content = body.get("content", "")
        title = body.get("title")
        tags = body.get("tags")
        facts = body.get("facts")
        if not slug or not content:
            _send_error(handler, 400, "slug and content required")
            return
        try:
            from wubu_wiki import Wiki
            wiki = Wiki()
            changed = wiki.upsert(slug, content, title=title, tags=tags,
                                  source="api", facts=facts)
            _send_json(handler, {"ok": True, "changed": changed, "slug": slug})
        except Exception as e:
            _send_error(handler, 500, str(e))

    else:
        _send_error(handler, 404, "not found")


# Global hotkey events buffer (shared between bridge and gateway)
_hotkey_events = []


class GatewayHandler(BaseHTTPRequestHandler):
    """HTTP handler for the AGI API gateway."""

    def log_message(self, *args):
        pass  # suppress default logging

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Authorization,Content-Type")
        self.end_headers()

    def do_GET(self):
        if not _check_auth(self.headers):
            _send_error(self, 401, "unauthorized")
            return
        _handle_api_get(self)

    def do_POST(self):
        if not _check_auth(self.headers):
            _send_error(self, 401, "unauthorized")
            return
        length = int(self.headers.get("Content-Length", 0))
        try:
            body = json.loads(self.rfile.read(length) or "{}")
        except Exception:
            _send_error(self, 400, "invalid JSON")
            return
        _handle_api_post(self, body)


def run_server(port=PORT):
    """Start the API gateway server."""
    # Link to bridge's hotkey events
    global _hotkey_events
    try:
        import wubu_bridge
        # We can't directly share the list, but we can use the bridge's HTTP API
    except Exception:
        pass

    server = HTTPServer(("127.0.0.1", port), GatewayHandler)
    print(f"[gateway] API server on http://127.0.0.1:{port}", flush=True)
    print(f"[gateway] endpoints:", flush=True)
    print(f"  GET  /api/health", flush=True)
    print(f"  GET  /api/stats", flush=True)
    print(f"  GET  /api/cookies?domain=github.com", flush=True)
    print(f"  POST /api/say {{text, mood}}", flush=True)
    print(f"  POST /api/poke {{power}}", flush=True)
    print(f"  POST /api/fling {{power}}", flush=True)
    print(f"  GET  /api/wiki/search?q=YUY2", flush=True)
    print(f"  GET  /api/wiki/fact?key=capture.yuy2_latency_ms", flush=True)
    print(f"  GET  /api/wiki/article/<slug>", flush=True)
    print(f"  GET  /api/wiki/stats", flush=True)
    print(f"  GET  /api/capture/devices", flush=True)
    print(f"  POST /api/capture/optimize {{format}}", flush=True)
    print(f"  GET  /api/hotkeys", flush=True)
    if TOKEN:
        print(f"[gateway] auth: Bearer token required", flush=True)
    else:
        print(f"[gateway] WARNING: no token (dev mode)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    run_server()

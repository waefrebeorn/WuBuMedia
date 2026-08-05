#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_face.py — Serve the cohost face overlay over localhost.

The OBS browser source points at http://127.0.0.1:8137/ to render the
animated cohost avatar. This server:
  * Serves static files from WuBuMedia/face/ (index.html, stage.json)
  * Provides /face_state.json (CORS-friendly, no-cache, gzip-compressed)
  * Provides /buddy_interaction.json (receive grab/poke/fling from overlay)
  * Serves /health for OBS/guard monitoring
  * Gzip compression for larger assets (index.html is ~44KB)
  * Auto-starts in the WuBuMedia venv (no manual setup)

Usage:
  python -m wubu_face            # serves on 127.0.0.1:8137
  python -m wubu_face --port 8138  # alternate port

The face directory is configurable via WUBU_FACE_DIR (default: ../face).
"""
import argparse
import gzip
import json
import os
import sys
import time
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.dirname(HERE)
FACE_DIR = os.environ.get("WUBU_FACE_DIR",
                          os.path.join(WUBUMEDIA, "face"))

# MIME types for common overlay assets
MIME = {
    ".html": ("text/html", False),
    ".json": ("application/json", False),
    ".js": ("application/javascript", True),
    ".css": ("text/css", True),
    ".png": ("image/png", True),
    ".jpg": ("image/jpeg", True),
    ".svg": ("image/svg+xml", True),
    ".ico": ("image/x-icon", True),
    ".wav": ("audio/wav", True),
    ".mp3": ("audio/mpeg", True),
}


class FaceHandler(SimpleHTTPRequestHandler):
    """Static file handler rooted at FACE_DIR + API endpoints.

    Gzip-compresses text assets when the client supports it (Accept-Encoding:
    gzip). JSON state files get no-cache + CORS headers so the overlay never
    serves stale state. POST /buddy_interaction.json receives grab/poke/fling
    events from the avatar and writes them for the cohost loop to consume.
    """

    def __init__(self, *args, **kwargs):
        self._no_cache = False
        self._gzip = False
        super().__init__(*args, directory=FACE_DIR, **kwargs)

    def translate_path(self, path):
        """Map URL path to a file under FACE_DIR."""
        path = unquote(path)
        if path.startswith("/face_state.json"):
            return os.path.join(FACE_DIR, "face_state.json")
        if path.startswith("/stage.json"):
            return os.path.join(FACE_DIR, "stage.json")
        if path.startswith("/buddy_interaction.json"):
            return os.path.join(FACE_DIR, "buddy_interaction.json")
        # /health endpoint
        if path == "/health":
            return None  # handled in do_GET
        # Default: serve from face dir
        rel = path.lstrip("/")
        if rel in ("", "index.html"):
            rel = "index.html"
        return os.path.join(FACE_DIR, rel)

    def end_headers(self):
        """Inject no-cache + CORS headers before the header terminator."""
        if getattr(self, "_no_cache", False):
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.send_header("Access-Control-Allow-Origin", "*")
        if getattr(self, "_gzip", False):
            self.send_header("Content-Encoding", "gzip")
        super().end_headers()

    def do_GET(self):
        """Serve static files + JSON state + health endpoint."""
        self._no_cache = self.path.endswith(".json")
        self._gzip = False

        # /health — lightweight health check for OBS/guard monitoring
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            state = {"status": "ok", "uptime": time.time() - _START_TIME}
            # Merge in face state fields if available
            fp = os.path.join(FACE_DIR, "face_state.json")
            try:
                with open(fp) as f:
                    state.update(json.load(f))
            except Exception:
                state["face_state"] = "not found"
            self.wfile.write(json.dumps(state).encode())
            return

        # Check for gzip support
        accept = self.headers.get("Accept-Encoding", "")
        if "gzip" in accept:
            self._gzip = True

        # Let the parent serve the file, then wrap with gzip if needed
        path = self.translate_path(self.path)
        if path and os.path.isfile(path):
            # Check if we should gzip this file
            ext = os.path.splitext(path)[1].lower()
            if ext in (".html", ".js", ".css", ".json", ".svg"):
                try:
                    raw = open(path, "rb").read()
                    if len(raw) > 200:  # only compress worthwhile files
                        compressed = gzip.compress(raw, compresslevel=6)
                        if len(compressed) < len(raw) * 0.9:
                            self.send_response(200)
                            ct = MIME.get(ext, ("application/octet-stream", True))[0]
                            self.send_header("Content-Type", ct)
                            self.send_header("Content-Length", str(len(compressed)))
                            self.end_headers()
                            self.wfile.write(compressed)
                            return
                except Exception:
                    pass  # fall through to normal serving
        # Default: let parent handle
        return super().do_GET()

    def do_POST(self):
        """Receive interaction events from the overlay."""
        if self.path.startswith("/buddy_interaction.json"):
            try:
                length = int(self.headers.get("Content-Length", 0))
                data = json.loads(self.rfile.read(length) or "{}")
                path = os.path.join(FACE_DIR, "buddy_interaction.json")
                tmp = path + ".tmp"
                with open(tmp, "w") as f:
                    json.dump(data, f)
                os.replace(tmp, path)  # atomic
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"ok")
                return
            except Exception as e:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(str(e).encode())
                return
        self.send_response(404)
        self.end_headers()

    def log_message(self, fmt, *args):
        if os.environ.get("WUBU_FACE_DEBUG"):
            sys.stderr.write("[face] %s - %s\n" % (self.address_string(), fmt % args))


_START_TIME = time.time()


def main():
    ap = argparse.ArgumentParser(description="WuBuDesk face overlay server")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int,
                    default=int(os.environ.get("WUBU_FACE_PORT", "8137")))
    args = ap.parse_args()

    # Startup validation: ensure face dir + index.html exist
    os.makedirs(FACE_DIR, exist_ok=True)
    index_path = os.path.join(FACE_DIR, "index.html")
    if not os.path.exists(index_path):
        print(f"[face] WARN: index.html not found in {FACE_DIR}", file=sys.stderr)

    # Ensure face_state.json exists (so the overlay doesn't 404 on first poll)
    state_path = os.path.join(FACE_DIR, "face_state.json")
    if not os.path.exists(state_path):
        default = {"mood": "happy", "text": "", "speaking": False,
                   "ts": time.time(), "mode": "live", "visemes": ""}
        tmp = state_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(default, f)
        os.replace(tmp, state_path)

    print(f"[face] serving {FACE_DIR} on http://{args.host}:{args.port}/", flush=True)
    print(f"[face] health endpoint: http://{args.host}:{args.port}/health", flush=True)
    httpd = HTTPServer((args.host, args.port), FaceHandler)
    httpd.serve_forever()


if __name__ == "__main__":
    main()

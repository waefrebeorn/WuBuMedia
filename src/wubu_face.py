#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""
wubu_face.py — Serve the cohost face overlay over localhost.

The OBS browser source points at http://127.0.0.1:8137/ to render the
animated cohost avatar. This server:
  * Serves static files from WuBuMedia/face/ (index.html, stage.json)
  * Provides /face_state.json (CORS-friendly, no-cache)
  * Provides /buddy_interaction.json (receive grab/poke/fling from the overlay)
  * Auto-starts in the WuBuMedia venv (no manual setup)

Usage:
  python -m wubu_face            # serves on 127.0.0.1:8137
  python -m wubu_face --port 8138  # alternate port

The face directory is configurable via WUBU_FACE_DIR (default: ../face).
"""
import argparse
import json
import os
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.dirname(HERE)
FACE_DIR = os.environ.get("WUBU_FACE_DIR",
                          os.path.join(WUBUMEDIA, "face"))


class FaceHandler(SimpleHTTPRequestHandler):
    """Static file handler rooted at FACE_DIR + API endpoints."""

    def translate_path(self, path):
        """Map URL path to a file under FACE_DIR."""
        path = unquote(path)
        if path.startswith("/face_state.json"):
            return os.path.join(FACE_DIR, "face_state.json")
        if path.startswith("/stage.json"):
            return os.path.join(FACE_DIR, "stage.json")
        if path.startswith("/buddy_interaction.json"):
            return os.path.join(FACE_DIR, "buddy_interaction.json")
        # Default: serve from face dir
        rel = path.lstrip("/")
        if rel in ("", "index.html"):
            rel = "index.html"
        return os.path.join(FACE_DIR, rel)

    def send_head(self):
        """Override to add no-cache headers for JSON state files."""
        # SimpleHTTPRequestHandler.send_head returns a file object.
        # We inject Cache-Control before headers are finalized.
        if self.path.endswith(".json"):
            # We'll add the header in end_headers by stashing it
            self._no_cache = True
        else:
            self._no_cache = False
        return super().send_head()

    def end_headers(self):
        """Inject no-cache headers before the header terminator."""
        if getattr(self, "_no_cache", False):
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def do_GET(self):
        """Serve static files + JSON state (with no-cache for .json)."""
        self._no_cache = self.path.endswith(".json")
        return super().do_GET()

    def do_POST(self):
        """Receive interaction events from the overlay."""
        if self.path.startswith("/buddy_interaction.json"):
            try:
                length = int(self.headers.get("Content-Length", 0))
                data = json.loads(self.rfile.read(length) or "{}")
                path = os.path.join(FACE_DIR, "buddy_interaction.json")
                with open(path, "w") as f:
                    json.dump(data, f)
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
        sys.stderr.write("[face] %s - %s\n" % (self.address_string(), fmt % args))


def main():
    ap = argparse.ArgumentParser(description="WuBuDesk face overlay server")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=int(os.environ.get("WUBU_FACE_PORT", "8137")))
    args = ap.parse_args()

    os.makedirs(FACE_DIR, exist_ok=True)

    # Ensure face_state.json exists (so the overlay doesn't 404 on first poll)
    state_path = os.path.join(FACE_DIR, "face_state.json")
    if not os.path.exists(state_path):
        with open(state_path, "w") as f:
            json.dump({"mood": "happy", "text": "", "speaking": False, "ts": 0}, f)

    print(f"[face] serving {FACE_DIR} on http://{args.host}:{args.port}/")
    httpd = HTTPServer((args.host, args.port), FaceHandler)
    httpd.serve_forever()


if __name__ == "__main__":
    main()

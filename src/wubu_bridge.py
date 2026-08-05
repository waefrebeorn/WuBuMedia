#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_bridge.py — Browser cookie bridge + hotkey listener.

Phase 7: Integration (browser cookies + system control).

This is the cohost's gateway to the real computer:
  * Reads browser cookies (Chrome/Edge/Firefox) via browser_cookie3
  * Exposes them to the OBS browser source via HTTP (with CORS)
  * Listens for system-wide hotkeys via Win32 RegisterHotKey
  * Forwards hotkey events to the cohost's persona

Research:
  * browser_cookie3: Decrypts Chrome/Edge cookies via DPAPI + AES-GCM
    (https://github.com/borisbabic/browser_cookie3)
  * Win32 RegisterHotKey: System-wide hotkey registration
    (https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)
  * WebSocket push already in wubu_wss.py for real-time state

Usage:
  python src/wubu_bridge.py

  Endpoints:
  * GET /cookies?domain=github.com — returns cookies for that domain
  * GET /hotkeys — returns active hotkey bindings
  * POST /hotkey/<id> — triggers a hotkey action

Config:
  * WUBU_BRIDGE_TOKEN — security token for browser source
  * WUBU_BRIDGE_PORT — port (default: 8162)
  * TWITCH_OAUTH, TWITCH_NICK, TWITCH_CHANNEL — Twitch IRC

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import sys
import time
import hmac
import threading
import subprocess
import webbrowser
import hashlib
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Security token — must match the OBS browser source
TOKEN = os.environ.get("WUBU_BRIDGE_TOKEN", "")

# Port for the bridge server
PORT = int(os.environ.get("WUBU_BRIDGE_PORT", "8162"))

# Rolling buffer of recent hotkey triggers
_hotkey_events = []
_hotkey_lock = threading.Lock()

# Hotkey definitions: (id, description, action)
HOTKEYS = [
    ("ctrl+shift+w", "Wake the cohost", "wake"),
    ("ctrl+shift+s", "Stream snip current screen", "snip"),
    ("ctrl+shift+m", "Mute cohost voice", "mute"),
    ("ctrl+shift+h", "Force health check", "health"),
    ("ctrl+shift+b", "Toggle buddy visibility", "toggle_buddy"),
]


def read_browser_cookies(domain=None):
    """Read cookies from the user's default browser.

    Uses browser_cookie3 which decrypts Chrome/Edge cookies via DPAPI
    and Firefox cookies directly. Falls back to manual Chrome SQLite
    reading if browser_cookie3 isn't installed.

    Returns a list of {"name": str, "value": str, "domain": str} dicts.
    """
    try:
        import browser_cookie3
        # Merge Chrome + Edge cookies (Chrome has priority)
        cj = browser_cookie3.chrome(domain_name=domain)
        cookies = [{"name": c.name, "value": c.value, "domain": c.domain}
                   for c in cj]
        if domain:
            cookies = [c for c in cookies if domain in c["domain"]]
        return cookies
    except ImportError:
        # Fallback: direct SQLite read from Chrome cookies DB (unencrypted part only)
        pass
    except Exception as e:
        print(f"[bridge] browser_cookie3 error: {e}", file=sys.stderr)
        return []
    # Manual fallback: read Chrome cookies SQLite (value field only, not encrypted)
    return _read_chrome_sqlite(domain)


def _read_chrome_sqlite(domain=None):
    """Fallback: read Chrome cookies SQLite directly (unencrypted values only)."""
    try:
        import sqlite3
        cookie_path = os.path.expandvars(
            r"%LOCALAPPDATA%\Google\Chrome\User Data\Default\Cookies")
        if not os.path.exists(cookie_path):
            return []
        conn = sqlite3.connect(cookie_path)
        conn.row_factory = sqlite3.Row
        query = "SELECT name, value, host_key FROM cookies"
        if domain:
            query += f" WHERE host_key LIKE '%{domain}%'"
        rows = conn.execute(query).fetchall()
        conn.close()
        return [{"name": r["name"], "value": r["value"],
                 "domain": r["host_key"]} for r in rows]
    except Exception as e:
        print(f"[bridge] chrome sqlite read: {e}", file=sys.stderr)
        return []


def try_win_hotkeys():
    """Register system-wide hotkeys using Win32 API.

    Uses ctypes to call RegisterHotBuffer. Only works on Windows.
    Falls back to global-hotkeys package if available.
    """
    if sys.platform != "win32":
        print("[bridge] hotkeys require Windows", flush=True)
        return False

    try:
        import ctypes
        from ctypes import wintypes

        user32 = ctypes.windll.user32
        # Hotkey IDs (must be unique)
        HOTKEY_IDS = {
            1: ("ctrl+shift+w", "wake"),
            2: ("ctrl+shift+s", "snip"),
            3: ("ctrl+shift+m", "mute"),
            4: ("ctrl+shift+h", "health"),
            5: ("ctrl+shift+b", "toggle_buddy"),
        }

        # Modifiers: MOD_CONTROL=0x2, MOD_SHIFT=0x4
        MOD_CONTROL = 0x2
        MOD_SHIFT = 0x4

        # Virtual key codes
        VK = {"W": 0x57, "S": 0x53, "M": 0x4D, "H": 0x48, "B": 0x42}

        def trigger_hotkey(action):
            with _hotkey_lock:
                _hotkey_events.append({
                    "action": action,
                    "timestamp": time.time(),
                })
                if len(_hotkey_events) > 50:
                    _hotkey_events.pop(0)

        # Register each hotkey
        for hid, (combo, action) in HOTKEY_IDS.items():
            # Parse combo like "ctrl+shift+w"
            modifiers = 0
            keys = combo.split("+")
            key = keys[-1].upper()
            for mod in keys[:-1]:
                if mod == "ctrl":
                    modifiers |= MOD_CONTROL
                elif mod == "shift":
                    modifiers |= MOD_SHIFT

            result = user32.RegisterHotKey(None, hid, modifiers, VK[key])
            if result:
                print(f"  registered: {combo} -> {action}", flush=True)
            else:
                print(f"  FAILED to register: {combo}", flush=True)

        # Message loop
        MSG = wintypes.MSG
        msg = MSG()
        while True:
            if user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
                if msg.message == 0x0312:  # WM_HOTKEY
                    hid = msg.wParam
                    if hid in HOTKEY_IDS:
                        trigger_hotkey(HOTKEY_IDS[hid][1])
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))
            elif msg.message == 0x100:  # WM_NULL (shouldn't happen normally)
                pass

        return True
    except ImportError:
        pass
    except AttributeError:
        pass

    # Fallback: try global-hotkeys package
    try:
        import global_hotkeys
        for combo, action in HOTKEYS:
            def make_cb(a):
                def cb():
                    trigger_hotkey(a)
                return cb
            global_hotkeys.registerHotkey(combo, callback=make_cb(action))
        global_hotkeys.startListening()
        return True
    except ImportError:
        print("[bridge] no hotkey library available (ctypes + global_hotkeys)", flush=True)
        return False


def _start_hotkeys():
    """Start the hotkey listener in a background thread."""
    def hotkey_thread():
        try_win_hotkeys()
    t = threading.Thread(target=hotkey_thread, daemon=True)
    t.start()
    print("[bridge] hotkey listener started", flush=True)


class BridgeHandler(BaseHTTPRequestHandler):
    """HTTP handler for the browser cookie + hotkey bridge.

    All endpoints require the WUBU_BRIDGE_TOKEN for security.
    """

    def _send_json(self, data, status=200, cors=True):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        if cors:
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Headers", "Authorization")
        self.end_headers()
        self.wfile.write(body)

    def _check_auth(self):
        """Verify the request has the correct token in the Authorization header."""
        if not TOKEN:
            return True  # no token configured = open (dev mode)
        auth = self.headers.get("Authorization", "")
        if auth.startswith("Bearer "):
            token = auth[7:]
            return hmac.compare_digest(token, TOKEN)
        return False

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        qs = parse_qs(parsed.query)

        if not self._check_auth():
            self._send_json({"error": "unauthorized"}, status=401)
            return

        if path == "/health":
            self._send_json({"ok": True, "ts": time.time()})

        elif path == "/cookies":
            domain = qs.get("domain", [None])[0]
            cookies = read_browser_cookies(domain=domain)
            self._send_json({
                "count": len(cookies),
                "cookies": cookies[:100],  # cap at 100
            })

        elif path == "/hotkeys":
            with _hotkey_lock:
                events = list(_hotkey_events[-20:])
            self._send_json({
                "bindings": [h[0] for h in HOTKEYS],
                "recent_events": events,
            })

        elif path == "/stats":
            from wubu_agent import stats
            self._send_json(stats())

        else:
            self._send_json({"error": "not found"}, status=404)

    def do_OPTIONS(self):
        """CORS preflight for browser source."""
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Authorization")
        self.end_headers()

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if not self._check_auth():
            self._send_json({"error": "unauthorized"}, status=401)
            return

        if path == "/hotkey/trigger":
            length = int(self.headers.get("Content-Length", 0))
            data = json.loads(self.rfile.read(length) or "{}")
            action = data.get("action", "")
            print(f"[bridge] hotkey triggered: {action}", flush=True)
            self._send_json({"accepted": action})
        else:
            self._send_json({"error": "not found"}, status=404)

    def log_message(self, *args):
        """Suppress default logging — cohost handles its own logging."""
        pass


def run_server(port=PORT):
    """Start the HTTP bridge server and hotkey listener."""
    _start_hotkeys()
    server = HTTPServer(("127.0.0.1", port), BridgeHandler)
    print(f"[bridge] listening on http://127.0.0.1:{port}", flush=True)
    if TOKEN:
        print("[bridge] auth: Bearer token required", flush=True)
    else:
        print("[bridge] WARNING: no token — open access (dev only)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    run_server()

#!/usr/bin/env python3
"""
wubu_native_host.py — Chrome/Edge Native Messaging Host for the WuBuDesk bridge.

The extension (browser/extension/) talks to THIS host over native messaging
(stdio JSON frames). The host relays to the agent's localhost WebSocket bridge
(wubu_bridge.py) or handles simple page-context requests itself.

Security (per BRIDGE_SPEC.md):
  * Localhost only — no network egress.
  * Token-gated: every request must carry WUBU_BRIDGE_TOKEN (from Hermes .env).
  * Read-only page context; never passwords/cookies (extension redacts them).
  * Host manifest must be registered in the browser registry (see register_host).

Native messaging protocol: each message is 4-byte little-endian length + JSON.
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import struct
import sys

ENV = os.path.expandvars(r"C:\Users\eman5\AppData\Local\hermes\profiles\wubudesk\.env")


def load_token():
    tok = None
    try:
        for line in open(ENV, encoding="utf-8"):
            if line.strip().startswith("WUBU_BRIDGE_TOKEN="):
                tok = line.strip().split("=", 1)[1]
    except Exception:
        pass
    return tok


TOKEN = load_token() or os.environ.get("WUBU_BRIDGE_TOKEN")


def read_msg():
    raw = sys.stdin.buffer.read(4)
    if not raw or len(raw) < 4:
        return None
    (length,) = struct.unpack("@I", raw)
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def write_msg(obj):
    data = json.dumps(obj).encode("utf-8")
    sys.stdout.buffer.write(struct.pack("@I", len(data)))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def handle(msg):
    if not TOKEN or msg.get("token") != TOKEN:
        return {"error": "bad_token"}
    cmd = msg.get("cmd")
    if cmd == "ping":
        return {"result": "pong", "badgeOn": True}
    if cmd == "getContext":
        # In the full loop this proxies to wubu_bridge.py on 127.0.0.1:18765.
        # The extension already returns title/url/visibleText; we pass through.
        return {"result": {"note": "page context relayed by extension",
                           "redacted": ["password", "cookie"]}}
    return {"error": "unknown_cmd"}


def main():
    while True:
        msg = read_msg()
        if msg is None:
            break
        write_msg(handle(msg))


if __name__ == "__main__":
    main()

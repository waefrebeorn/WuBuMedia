#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_wss.py — WebSocket server that pushes face_state.json to the overlay.

Instead of the browser polling face_state.json every 420ms (which adds
latency on stream), this server pushes updates in real-time via WebSocket.

The cohost writes face_state.json atomically (temp + os.replace), then
notifies this server which pushes the new state to all connected browsers.

Usage:
  python src/wubu_wss.py              # serves on 127.0.0.1:8138
  python src/wubu_wss.py --port 8139  # alternate port

The browser opens a WebSocket to ws://<host>:<port>/state/ and receives
JSON messages whenever face_state.json changes.

No external dependencies — uses only the standard library.
"""
import argparse
import asyncio
import hashlib
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.dirname(HERE)
FACE_DIR = os.environ.get("WUBU_FACE_DIR",
                          os.path.join(WUBUMEDIA, "face"))

# Globals
_clients = set()
_last_state = None
_last_mtime = 0


def read_face_state():
    """Read and return face_state.json, or None if it doesn't exist."""
    path = os.path.join(FACE_DIR, "face_state.json")
    try:
        st = os.stat(path)
        if st.st_mtime <= _last_mtime:
            return None
        with open(path) as f:
            return json.load(f)
    except Exception:
        return None


async def broadcast(state):
    """Push state to all connected WebSocket clients."""
    if not state:
        return
    msg = json.dumps(state)
    dead = set()
    for ws in _clients:
        try:
            ws.send(msg)
        except Exception:
            dead.add(ws)
    _clients -= dead


async def state_watcher():
    """Poll face_state.json for changes and push to all clients."""
    global _last_mtime, _last_state
    while True:
        state = read_face_state()
        if state is not None:
            path = os.path.join(FACE_DIR, "face_state.json")
            try:
                mtime = os.stat(path).st_mtime
                if mtime > _last_mtime:
                    _last_mtime = mtime
                    _last_state = state
                    await broadcast(state)
            except Exception:
                pass
        await asyncio.sleep(0.05)  # 50ms polling — imperceptible


class WSHandler:
    """Minimal WebSocket handler (RFC 6455) — no dependencies."""

    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.ws = None

    async def handle(self):
        # Read the HTTP/WebSocket upgrade request
        request = b""
        while b"\r\n\r\n" not in request:
            chunk = await self.reader.read(4096)
            if not chunk:
                return
            request += chunk

        # Parse headers
        headers = request.split(b"\r\n")
        if len(headers) < 2:
            return

        # Check for WebSocket upgrade
        has_upgrade = False
        key = None
        for h in headers:
            if b"Upgrade:" in h or b"upgrade:" in h:
                has_upgrade = True
            if b"Sec-WebSocket-Key:" in h or b"sec-websocket-key:" in h:
                key = h.split(b":", 1)[1].strip().decode()

        if not has_upgrade or not key:
            self.writer.close()
            return

        # Perform WebSocket handshake
        guid = "258EAEDA-E62A-439B-B0A4-F3F6D6D2B518"
        accept = hashlib.sha1((key + guid).encode()).hexdigest()
        accept = __import__("base64").b64encode(accept.encode()).decode()

        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n"
            "\r\n"
        )
        self.writer.write(response.encode())
        await self.writer.drain()

        # Register as a client
        ws = WebSocketConn(self.reader, self.writer)
        _clients.add(ws)
        print(f"[wss] client connected ({len(_clients)} total)", flush=True)

        try:
            # Push current state immediately
            if _last_state:
                ws.send(json.dumps(_last_state))

            # Keep connection alive
            await ws.handle_messages()
        finally:
            _clients.discard(ws)
            print(f"[wss] client disconnected ({len(_clients)} remaining)", flush=True)


class WebSocketConn:
    """A WebSocket connection that can send and receive messages."""

    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self._closed = False

    def send(self, msg):
        """Send a text message (no masking for server->client)."""
        if self._closed:
            return
        data = msg.encode()
        # Frame: FIN=1, opcode=1 (text), no mask, length
        frame = bytearray()
        frame.append(0x81)  # 10000001: FIN + text frame
        if len(data) < 126:
            frame.append(len(data))
        elif len(data) < 65536:
            frame.append(126)
            frame.extend(len(data).to_bytes(2, "big"))
        else:
            frame.append(127)
            frame.extend(len(data).to_bytes(8, "big"))
        frame.extend(data)
        self.writer.write(bytes(frame))

    async def handle_messages(self):
        """Read and discard incoming frames (keep-alive + ping/pong)."""
        try:
            while not self._closed:
                try:
                    # Read frame header
                    header = await self.reader.readexactly(2)
                    if len(header) < 2:
                        break
                    fin = (header[0] & 0x80) != 0
                    opcode = header[0] & 0x0F
                    masked = (header[1] & 0x80) != 0
                    length = header[1] & 0x7F

                    if opcode == 8:  # close
                        break
                    if opcode == 9:  # ping -> send pong
                        continue

                    # Read extended length
                    if length == 126:
                        length_bytes = await self.reader.readexactly(2)
                        length = int.from_bytes(length_bytes, "big")
                    elif length == 127:
                        length_bytes = await self.reader.readexactly(8)
                        length = int.from_bytes(length_bytes, "big")

                    # Read masking key if present
                    mask = None
                    if masked:
                        mask = await self.reader.readexactly(4)

                    # Read payload
                    payload = await self.reader.readexactly(length)
                    if masked and mask:
                        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
                except (asyncio.IncompleteReadError, ConnectionResetError):
                    break
        except Exception:
            pass
        finally:
            self._closed = True


async def handle_client(reader, writer):
    handler = WSHandler(reader, writer)
    await handler.handle()


def main():
    ap = argparse.ArgumentParser(description="WuBuDesk WebSocket push server")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int,
                    default=int(os.environ.get("WUBU_WSS_PORT", "8138")))
    args = ap.parse_args()

    os.makedirs(FACE_DIR, exist_ok=True)
    print(f"[wss] serving on ws://{args.host}:{args.port}/state/", flush=True)

    asyncio.ensure_future(state_watcher())

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    server = loop.run_until_complete(
        asyncio.start_server(handle_client, args.host, args.port))
    print(f"[wss] listening on {args.host}:{args.port}", flush=True)
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        print("\n[wss] shutting down", flush=True)
    finally:
        server.close()
        loop.run_until_complete(server.wait_closed())
        loop.close()


if __name__ == "__main__":
    main()

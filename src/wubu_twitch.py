#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_twitch.py — Twitch IRC chat integration for the cohost.

Phase 6: Professional Streaming Companion.

Connects to Twitch IRC (irc.chat.twitch.tv:6697) and feeds chat messages
to the cohost's persona. The cohost reacts to chat hype (emotes, bits,
subs) as a third voice in the room, not just the streamer.

Research: Twitch IRC protocol (dev.twitch.tv/docs/chat/irc)
  * PRIVMSG format: @tags :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :msg
  * Tags include badges, bits, subscriber, vip
  * CAP REQ :twitch.tv/tags adds tag data to messages
  * CAP REQ :twitch.tv/commands enables IRC commands (JOIN, PART, etc.)
  * User notices (subs, raids) come as USERNOTICE messages

Usage:
  from wubu_twitch import TwitchChat
  chat = TwitchChat(channel="waefrebeorn")
  chat.start(on_message)  # callback(msg_dict) called per chat message
  chat.stop()

  msg_dict = {
      "nick": "viewer123",
      "text": "POGCHAMP sick play!",
      "badges": ["broadcaster/1"],
      "bits": 100,        # only present for cheer messages
      "subscriber": True, # True/False
      "vip": True,        # True/False
  }

Config: TWITCH_OAUTH (OAuth token), TWITCH_NICK (bot username),
        TWITCH_CHANNEL (channel to join). All from Hermes profile .env.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import ssl
import socket
import time
import threading
import re

# Default Twitch IRC connection
TWITCH_IRC = "irc.chat.twitch.tv"
TWITCH_PORT = 6697
TWITCH_NICK = os.environ.get("TWITCH_NICK", "")
TWITCH_OAUTH = os.environ.get("TWITCH_OAUTH", "")  # "oauth:xxxxx"
TWITCH_CHANNEL = os.environ.get("TWITCH_CHANNEL", "")

# Regex to parse a Twitch PRIVMSG with or without tags
# With tags: @tag1=val;tag2=val; :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :msg
# Without tags: :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :msg
_PRIVMSG_RE = re.compile(
    r"(?:^@([^ ]+) )?:([^!]+)![^@]+@[^ ]+ PRIVMSG #([^ ]+) :(.*)$"
)


def parse_tags(tag_str):
    """Parse @key=val;key=val -> dict."""
    tags = {}
    if not tag_str:
        return tags
    for pair in tag_str.split(";"):
        if "=" in pair:
            k, v = pair.split("=", 1)
            tags[k] = v
    return tags


def parse_message(line):
    """Parse a raw IRC line into a message dict.

    Returns None for non-chat messages (PING, JOIN, etc.).
    Returns dict for PRIVMSG and USERNOTICE.
    """
    # PING keepalive — must respond
    if line.startswith("PING"):
        return {"type": "PING"}

    # PRIVMSG (chat message)
    m = _PRIVMSG_RE.match(line)
    if m:
        tags = parse_tags(m.group(1)) if m.group(1) else {}
        nick = m.group(2)
        channel = m.group(3)
        text = m.group(4)
        # Strip Twitch emote formatting (\x01ACTION...\x01 for /me)
        if text.startswith("\x01ACTION") and text.endswith("\x01"):
            text = text[7:-1]

        badges = []
        if tags.get("badges"):
            badges = [b.split("/")[0] for b in tags["badges"].split(",")]

        msg = {
            "type": "PRIVMSG",
            "nick": nick,
            "channel": channel,
            "text": text,
            "badges": badges,
            "subscriber": tags.get("subscriber", "0") == "1",
            "mod": tags.get("mod", "0") == "1",
            "vip": tags.get("vip", "0") == "1",
            "broadcaster": "broadcaster" in badges,
        }
        if "bits" in tags:
            msg["bits"] = int(tags["bits"])
        return msg

    # USERNOTICE (subs, raids, etc.)
    if "USERNOTICE" in line:
        m = re.match(r'^@([^ ]+) :[^ ]+ USERNOTICE #([^ ]+)', line)
        if m:
            tags = parse_tags(m.group(1))
            event = tags.get("msg-id", "unknown")
            return {
                "type": "USERNOTICE",
                "event": event,
                "display_name": tags.get("display-name", ""),
                "login": tags.get("login", ""),
                "badges": [b.split("/")[0] for b in tags.get("badges", "").split(",")] if tags.get("badges") else [],
            }

    return None


class TwitchChat(threading.Thread):
    """Twitch IRC chat client running in a background thread.

    Connects to irc.chat.twitch.tv:6697 with SSL, joins a channel,
    and feeds parsed messages to on_message callback.

    Gracefully handles:
      * PING/PONG keepalive (Twitch pings every ~5 min)
      * Auto-reconnect on disconnect (with backoff)
      * Missing credentials (returns empty, doesn't crash)
    """

    def __init__(self, channel=None, nick=None, oauth=None,
                 on_message=None):
        super().__init__(daemon=True)
        self.channel = channel or TWITCH_CHANNEL
        self.nick = nick or TWITCH_NICK
        self.oauth = oauth or TWITCH_OAUTH
        self.on_message = on_message or (lambda m: None)
        self._sock = None
        self._running = False
        self._reconnect_delay = 5.0
        self.messages = []  # rolling buffer (last 50 chat messages)
        self._lock = threading.Lock()

    def run(self):
        """Main thread loop: connect → read → parse → callback → reconnect."""
        self._running = True
        while self._running:
            try:
                self._connect_and_loop()
            except Exception as e:
                print(f"[twitch] error: {e}", file=sys.stderr)
                time.sleep(self._reconnect_delay)
                self._reconnect_delay = min(60, self._reconnect_delay * 1.5)

    def _connect_and_loop(self):
        """Connect to Twitch IRC and process messages."""
        if not self.channel or not self.nick or not self.oauth:
            print("[twitch] missing TWITCH_NICK/OAUTH/CHANNEL — chat disabled",
                  file=sys.stderr)
            time.sleep(10)
            return

        ctx = ssl.create_default_context()
        # Twitch uses a certificate that might not be in the system trust store
        # on Windows Git Bash. Disable verification if needed.
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE

        try:
            raw = socket.create_connection(
                (TWITCH_IRC, TWITCH_PORT), timeout=30)
        except Exception as e:
            print(f"[twitch] connect failed: {e}", file=sys.stderr)
            time.sleep(self._reconnect_delay)
            return

        self._sock = ctx.wrap_socket(raw, server_hostname=TWITCH_IRC)
        self._sock.settimeout(300)  # 5 min — Twitch pings before this

        # Authenticate
        self._sock.sendall(f"PASS {self.oauth}\r\n".encode())
        self._sock.sendall(f"NICK {self.nick}\r\n".encode())
        # Request tags + commands capabilities
        self._sock.sendall("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n".encode())
        # Join channel
        self._sock.sendall(f"JOIN #{self.channel}\r\n".encode())

        print(f"[twitch] connected as {self.nick} -> #{self.channel}",
              flush=True)
        self._reconnect_delay = 5.0  # reset backoff on success

        buf = b""
        while self._running:
            try:
                data = self._sock.recv(4096)
                if not data:
                    break  # connection closed
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.rstrip(b"\r\n").decode("utf-8", errors="replace")

                    # Handle PING
                    if line.startswith("PING"):
                        self._sock.sendall(
                            f"PONG :{line.split(':')[1]}\r\n".encode())
                        continue

                    # Parse message
                    msg = parse_message(line)
                    if msg and msg["type"] in ("PRIVMSG", "USERNOTICE"):
                        with self._lock:
                            self.messages.append(msg)
                            if len(self.messages) > 50:
                                self.messages = self.messages[-50:]
                        try:
                            self.on_message(msg)
                        except Exception as e:
                            print(f"[twitch] callback error: {e}",
                                  file=sys.stderr)

            except socket.timeout:
                # Normal — Twitch pings every 5 min, socket timeout is the
                # fallback keepalive. Just loop and keep the connection alive.
                continue
            except Exception as e:
                print(f"[twitch] read error: {e}", file=sys.stderr)
                break

        self._sock.close()
        self._sock = None
        print("[twitch] disconnected, will reconnect...", flush=True)

    def recent_chat(self, n=10):
        """Return the last n chat messages as text strings."""
        with self._lock:
            recent = self.messages[-n:]
        return [m.get("text", "") for m in recent if m.get("type") == "PRIVMSG"]

    def stop(self):
        self._running = False
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass


if __name__ == "__main__":
    # Self-test: connect and print messages
    def on_msg(msg):
        if msg["type"] == "PRIVMSG":
            badges = ",".join(msg.get("badges", []))
            bits = f" [{msg['bits']} bits]" if msg.get("bits") else ""
            print(f"[{badges}] {msg['nick']}: {msg['text']}{bits}", flush=True)
        elif msg["type"] == "USERNOTICE":
            print(f"[notice] {msg['event']} from {msg.get('display_name','')}",
                  flush=True)

    chat = TwitchChat(on_message=on_msg)
    if not chat.channel:
        print("Set TWITCH_CHANNEL env var. Starting self-test in dry mode.")
        sys.exit(0)
    chat.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        chat.stop()
        print("\nGood, bye!")

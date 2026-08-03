# WuBuDesk Secure Browser Bridge — Design & Spec

> Local-only, token-gated, read-only page-context relay for the cohost CUA layer.
> License: SPDX-License-Identifier: WaefreBeorn-UMV3.

## Threat model (why it's built this way)
The boss explicitly said: *"secure plugin only."* A browser bridge lets the agent
see/act in the browser. The dangers:
- Exfiltration of page content (passwords, cookies, form fields) off-box.
- Remote control from anything other than localhost.
- Over-broad permissions (reading every tab silently).

## Security rules (non-negotiable)
1. **Localhost only.** The extension's native message / WS server binds to
   `127.0.0.1` (loopback). No `0.0.0.0`, no LAN exposure. Firewall off for it.
2. **Token gate.** Every command from the agent must carry a random token
   generated at install, stored only in the agent's local `.env`. Wrong token = drop.
3. **Read-only by default.** The bridge can READ page title/URL/visible text and
   click elements the agent names. It CANNOT read `<input type=password>` values,
   `document.cookie`, or `localStorage`. Those are redacted to `<redacted>`.
4. **No form injection of secrets.** Agent may type into fields, but the bridge
   logs nothing typed; agent is responsible (and the cohost never types creds).
5. **User-visible indicator.** A toolbar badge shows when the bridge is connected,
   so the boss always knows the cohost is "in" the browser.
6. **Open source, our license.** The boss can read every line before installing.

## Architecture
```
Edge/Chrome  <--native Messaging-->  wubu_bridge.exe (localhost WS :18765)
                                          |
                                      WuBuDesk agent (python)
```
- `manifest.json`: `host_permissions` limited to `<all_urls>` for reading, but the
  background script enforces redaction rules in code (defense in depth).
- `background.js`: receives `runtime.connectNative`, validates token, relays
  `getContext` / `clickSelector` / `scroll` commands, redacts secrets.
- `wubu_bridge.py`: localhost WS server; the only thing the agent talks to.
  Verifies token, talks to the extension via stdio native messaging.

## Commands (agent -> bridge)
- `getContext`  -> {title, url, visibleText (truncated), badgeOn}
- `clickSelector(sel)` -> clicks a named element (no coord guessing)
- `scroll(dir)` -> page scroll
- `activeTab`   -> current tab info only

## What it will NOT do
- No network egress except loopback.
- No reading of password fields / cookies / storage.
- No silent tab history upload.
- No remote (non-localhost) control.

This is the "secure hook into Edge/Chrome" the boss asked for. The boss installs
the unpacked extension; the bridge binary runs only on his machine.

# Windows Desktop Control — Knowledge Base (WuBuDesk)

> The cohost's working knowledge of how to actually USE the computer.
> License: SPDX-License-Identifier: WaefreBeorn-UMV3
> Companion tool: `src/wubucmd.c` (our own C11 desktop-control binary).

## 1. Coordinate system (the thing you MUST get right)

Windows has THREE coordinate spaces. Confusing them = clicks in the wrong place.

| Space | Origin | Unit | Used by |
|-------|--------|------|---------|
| **Screen (physical)** | top-left of VIRTUAL screen (bounding box of all monitors) | pixels | `GetWindowRect`, `SetCursorPos`, screenshots |
| **Client** | top-left of a window's client area | pixels | `GetClientRect`, most window APIs |
| **SendInput normalized** | 0..65535 in each axis across the virtual screen | normalized units | `SendInput` mouse |

### Multi-monitor reality
- `SM_XVIRTUALSCREEN/YVIRTUALSCREEN/CXVIRTUALSCREEN/CYVIRTUALSCREEN` = bounding
  rect of ALL monitors. Coordinates can be NEGATIVE (a monitor left of primary).
- On this rig: virtual = `(-1440,-512) 6320x3072` (3 monitors; left monitor at
  x=-1440..0, primary 0..3440, right 3440..4880).
- **Screenshot** must BitBlt from the virtual origin, not (0,0):
  `BitBlt(mem,0,0,vw,vh,hdc, vx,vy, SRCCOPY)`.
- **SendInput absolute click** converts physical pixels to normalized:
  `nx = (x - vx) * 65535 / (vw - 1)`, with flags
  `MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK`.
- `SetCursorPos(x,y)` is fine for physical pixels; SendInput needs normalized.

### Window state
- Minimized windows are parked at (-32000,-32000) — that's how you detect them.
- `IsIconic()` = minimized, `IsZoomed()` = maximized, `IsWindowVisible()`.
- ShowWindow flags: `SW_SHOWMINIMIZED=2, SW_SHOWMAXIMIZED=3, SW_MINIMIZE=6, SW_RESTORE=9`.

## 2. Input (mouse + keyboard)

- Real clicks: `SendInput` with `INPUT_MOUSE` (MOVE/LEFTDOWN/LEFTUP).
  SetCursorPos + SendInput click both work; SendInput is the "native" way.
- Keystrokes: `INPUT_KEYBOARD` with `KEYEVENTF_UNICODE` (text) — handles
  arbitrary Unicode incl. symbols. For keys (Enter/Esc), use `KEYEVENTF_SCANCODE`
  or VK codes.
- The foreground window receives keystrokes — `SetForegroundWindow` first.

## 3. Window management (the boss's explicit ask)

- **Find** a window: `FindWindowExW(NULL, prev, NULL, NULL)` walking top-levels,
  match title with `StrStrIW` (case-insensitive).
- **Focus**: `ShowWindow(SW_RESTORE)` then `SetForegroundWindow`.
- **Minimize**: `ShowWindow(SW_MINIMIZE)`; **Restore**: `SW_RESTORE`;
  **Maximize**: `SW_SHOWMAXIMIZED`.
- **Graceful close**: `SendMessageW(hwnd, WM_CLOSE, 0, 0)` — never force-kill a
  GUI app you care about (it leaves crash state).

## 4. GUI-app launch gotchas (learned the hard way)

1. **Working directory matters for some apps.** OBS resolves
   `locale/en-US.ini` relative to its CWD → launch with
   `-WorkingDirectory <app_bin_dir>` or you get "Failed to find locale".
2. **GUI apps need a real interactive session.** Launching from an agent shell
   (non-interactive window station) can fail D3D11/GPU init. OBS showed:
   `D3D11 GPU priority setup failed (not admin?)` then crashed.
   Proper: launch in the logged-in user's session (interactive task), ideally
   elevated for GPU priority.
3. **Force-killing a GUI app creates crash-recovery state.** OBS writes a crash
   log + shows "OBS Studio Crash Detected" next launch, which BLOCKS the
   websocket server (Safe Mode disables WebSockets!). Dismiss via
   "Run in Normal Mode" (UIAutomation/click) or delete
   `%APPDATA%\obs-studio\crashes\*`.
4. **obs-websocket**: config at
   `%APPDATA%\obs-studio\plugin_config\obs-websocket\config.json`
   (`server_enabled`, `server_port`, `server_password`). Module load line in the
   OBS log: `[obs-websocket] [obs_module_load] you can haz websockets`.

## 5. Our tool: wubucmd

```
wubucmd list                 enumerate visible windows
wubucmd win <substr>         rect + state of a window
wubucmd rect <substr>        print x y w h
wubucmd shot <file.bmp>      screenshot virtual screen
wubucmd focus <substr>       restore + foreground
wubucmd min|restore|max <s>  window state
wubucmd close <substr>       WM_CLOSE
wubucmd click <x> <y>        physical-pixel click (normalized SendInput)
wubucmd key <text>           Unicode keystrokes
```

Build: `gcc wubucmd.c -o wubucmd.exe -municode -lgdi32 -luser32 -lole32 -lshlwapi -O2`
(Tip: `-municode` is required for `wmain`.)

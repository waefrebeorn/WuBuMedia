#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_agent.py — The cohost's "hands and eyes" on the system.

Phase 4: AGI Master Computer Control.

This is what makes the cohost a real agent instead of a reactive plugin.
It lets the cohost:
  * Read system stats (CPU, GPU, memory, OBS state) — context for replies
  * Switch OBS scenes, mute sources, toggle recording/streaming
  * Trigger hotkeys to control the desktop (launch games, switch windows)
  * Read the screen via wubu_desktop + wubu_vision
  * Respond to global hotkeys (Ctrl+Alt+Chat triggers a cohost response)

Design principles:
  * Graceful everywhere — every function returns a default, never raises
    into the live loop. If the system call fails, the cohost still works.
  * Lazy imports — torch, psutil, keyboard only loaded when needed
  * No secrets logged — GPU names and stats only, never file paths of secrets

Usage:
  from wubu_agent import Agent
  agent = Agent(obs)  # obs = wubu_obs.ObsCohost() or wubu_stage.connect()
  agent.stats()       # -> dict of CPU/GPU/memory/OBS
  agent.mute_game()   # duck desktop audio
  agent.switch_scene("Gaming")
  agent.screenshot()  # -> PNG path

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import time

# Reuse existing modules
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    import wubu_desktop  # C11 wubucmd.exe + PowerShell fallback
    _HAS_DESKTOP = True
except Exception:
    _HAS_DESKTOP = False


def stats():
    """Gather system stats: CPU, GPU, memory, OBS state.

    All values are sampled once; no persistent monitoring. Returns a dict
    suitable for including in the face overlay HUD. Returns {} on any failure.

    Research: psutil for cross-platform stats, pynvml for NVIDIA GPU.
    We use lazy imports so psutil/pynvml are optional — degrades gracefully.
    """
    out = {}

    # CPU %
    try:
        import psutil
        out["cpu"] = round(psutil.cpu_percent(interval=0.2), 1)
        mem = psutil.virtual_memory()
        out["mem_pct"] = round(mem.percent, 1)
        out["mem_gb"] = round(mem.used / (1024**3), 2)
    except Exception:
        out["cpu"] = "n/a"

    # GPU stats (NVIDIA only — the rig has an RTX 2080 SUPER)
    try:
        import pynvml
        pynvml.nvmlInit()
        handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        util = pynvml.nvmlDeviceGetUtilization(handle)
        out["gpu_util"] = util.gpu
        out["gpu_mem"] = util.memory
        # GPU temperature
        try:
            temp = pynvml.nvmlDeviceGetTemperature(handle, pynvml.NVML_TEMPERATURE_GPU)
            out["gpu_temp"] = temp
        except Exception:
            pass
        # GPU name
        try:
            name = pynvml.nvmlDeviceGetName(handle)
            if isinstance(name, bytes):
                name = name.decode()
            out["gpu_name"] = name
        except Exception:
            pass
        pynvml.nvmlShutdown()
    except Exception:
        pass  # pynvml not installed or no NVIDIA GPU

    return out


def screenshot(path=None):
    """Take a screenshot of the primary display.

    Returns path to PNG file. Uses wubu_desktop.screenshot() which prefers
    the native C11 wubucmd.exe and falls back to PowerShell.
    """
    if _HAS_DESKTOP:
        try:
            return wubu_desktop.screenshot(path)
        except Exception as e:
            print(f"[agent] screenshot failed: {e}", file=sys.stderr)
    # Fallback: PIL ImageGrab
    try:
        from PIL import ImageGrab
        return ImageGrab.grab().save(path or "screenshot.png")
    except Exception:
        return None


def list_windows():
    """List all visible windows. Returns list of titles or [] on failure."""
    if _HAS_DESKTOP:
        try:
            return wubu_desktop.list_windows()
        except Exception:
            pass
    return []


def focus_window(substr):
    """Focus the first window whose title contains substr."""
    if _HAS_DESKTOP:
        try:
            return wubu_desktop.focus_window(substr)
        except Exception as e:
            print(f"[agent] focus failed: {e}", file=sys.stderr)
    return None


class Agent:
    """High-level control of the OBS + desktop.

    Wraps wubu_stage.wubu_stage (for OBS) and wubu_desktop (for Windows)
    into a single 'master AGI' interface that the cohost loop can query.

    All methods degrade gracefully — if OBS isn't connected, they return
    False/None/empty rather than raising.
    """

    def __init__(self, obs=None):
        self.obs = obs  # wubu_obs.ObsCohost or None
        self._stats_cache = None
        self._stats_ts = 0

    # -- OBS integration ----------------------------------------------------
    def mute_source(self, source, muted=True):
        """Mute/unmute an audio source in OBS."""
        if not self.obs:
            return False
        try:
            self.obs._call("SetSourceRender", sourceName=source,
                           render=not muted)
            return True
        except Exception:
            return False

    def mute_game(self, muted=True):
        """Mute desktop/game audio in OBS."""
        # The source name varies; try common ones
        for name in ("Desktop Audio", "Game Capture", "Audio Output Capture"):
            if self.mute_source(name, muted):
                return True
        return False

    def switch_scene(self, scene_name):
        """Switch to a named OBS scene."""
        if not self.obs:
            return False
        try:
            self.obs._call("SetCurrentProgramScene", sceneName=scene_name)
            return True
        except Exception:
            return False

    def list_scenes(self):
        """Return list of OBS scene names."""
        if not self.obs:
            return []
        try:
            scenes = self.obs._call("GetSceneList")
            return [s.get("name", "") for s in scenes.get("scenes", [])]
        except Exception:
            return []

    def start_recording(self):
        """Start OBS recording."""
        if not self.obs:
            return False
        try:
            self.obs._call("StartRecord")
            return True
        except Exception:
            return False

    def stop_recording(self):
        """Stop OBS recording."""
        if not self.obs:
            return False
        try:
            self.obs._call("StopRecord")
            return True
        except Exception:
            return False

    def toggle_stream(self):
        """Start or stop streaming (toggles)."""
        if not self.obs:
            return False
        try:
            status = self.obs._call("GetRecordStatus")
            if status.get("recording"):
                self.obs._call("StopStream")
            else:
                self.obs._call("StartStream")
            return True
        except Exception:
            return False

    def current_scene(self):
        """Return the name of the current OBS scene."""
        if not self.obs:
            return ""
        try:
            return self.obs._call("GetCurrentProgramScene").get("sceneName", "")
        except Exception:
            return ""

    # -- System stats -------------------------------------------------------
    def stats(self, cached=True):
        """Return system stats (cached for 2s to avoid hammering psutil)."""
        if cached and time.time() - self._stats_ts < 2:
            return self._stats_cache or {}
        s = stats()
        s["obs"] = bool(self.obs)
        try:
            s["obs_scene"] = self.current_scene()
        except Exception:
            pass
        self._stats_cache = s
        self._stats_ts = time.time()
        return s

    # -- Desktop control ----------------------------------------------------
    def see(self, prompt, max_tokens=120):
        """Take a screenshot and ask the vision model what's on it."""
        path = screenshot()
        if not path:
            return "I can't see the screen right now."
        try:
            import wubu_vision
            return wubu_vision.see(path, prompt, max_tokens=max_tokens) or ""
        except Exception:
            return ""

    def activate_window(self, substr):
        """Activate (foreground) a window by title substring."""
        return focus_window(substr)


# -- Global hotkey listener -----------------------------------------------
# Research: 'keyboard' library (boppreh) provides global keyboard hooks on
# Windows without admin privileges. We use Ctrl+Alt+B (Boss) to wake the
# cohost — it's unlikely to conflict with games or OBS.
def register_hotkeys(callback, hotkeys=None):
    """Register global hotkeys that trigger callback(action) when pressed.

    Args:
        callback: function(action) called when a hotkey fires
        hotkeys: dict of {hotkey_string: action_name}
                e.g. {"ctrl+alt+b": "wake", "ctrl+alt+p": "poke"}
                Returns True if registered, False if 'keyboard' lib unavailable.

    The 'keyboard' library requires no admin on Windows for global hooks,
    but may need to run in the main thread.
    """
    if hotkeys is None:
        hotkeys = {"ctrl+alt+b": "wake", "ctrl+alt+p": "poke"}
    try:
        import keyboard
        for hk, action in hotkeys.items():
            keyboard.add_hotkey(hk, callback, args=(action,),
                                suppress=False)  # don't suppress — let games use it
        return True
    except ImportError:
        print("[agent] 'keyboard' library not installed; hotkeys disabled",
              file=sys.stderr)
        return False
    except Exception as e:
        print(f"[agent] hotkey registration failed: {e}", file=sys.stderr)
        return False


if __name__ == "__main__":
    # Self-test: gather and print system stats
    s = stats()
    print("System stats:")
    for k, v in s.items():
        print(f"  {k}: {v}")
    print(f"\nVisible windows: {list_windows()[:5]}")
    print(f"Desktop module available: {_HAS_DESKTOP}")

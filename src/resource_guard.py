# SPDX-License-Identifier: WaefreBeorn-UMV3
#!/usr/bin/env python3
"""
resource_guard.py — WuBuDesk's resource-governance eye on the Stream Mega-Monolith.

PURPOSE (from boss directive 2026-08-03):
  The Windows box is HIS streaming + gaming rig. It runs LIVE while WuBuDesk
  works. The GPU/CPU/RAM are SHARED with his stream and games. Rules:
    * NEVER touch drivers / kernel / Windows install.
    * NEVER run heavy compute that steals GPU/CPU from his stream or game.
    * DEFER heavy work when he is streaming or gaming.
    * Linux agents do the risky OS-level stuff over there.

This module PROBES the box and returns a verdict + safe limits so every other
WuBuDesk action can ask "is it safe to do X right now?" before spending cycles.

Verdict states:
  IDLE       -> no OBS, no game, GPU cool  -> heavy work allowed (off-peak)
  STREAMING  -> obs64.exe up               -> light only; never touch GPU/encode
  GAMING     -> a known game exe up        -> pause non-essential work
  BUSY       -> high CPU/GPU but unclear   -> light only

safe_for_heavy: bool  (True only in IDLE and when GPU util < threshold)
limits: dict of recommended caps (max_cpu_pct, max_gpu_pct, max_parallel, can_use_gpu)

No secrets. Read-only probes only.
"""
import json
import shutil
import subprocess
import sys
import time

# Known game executables (extend as needed). Case-insensitive substring match.
GAME_EXES = [
    "eldenring", "cyberpunk", "fortnite", "rocketleague", "valorant",
    "csgo", "dota2", "witcher3", "starfield", "baldursgate3", "helldivers2",
    "apex", "pubg", "escapefromtarkov", "rustclient", "leagueoflegends",
    "blackmythwukong", "monsterhunter", "gta", "rdr2", "warframe",
]
STREAM_EXES = ["obs64.exe", "streamlabs", "obs32.exe", "prismlive"]
GPU_HEAVY_EXES = ["blender", "premiere", "aftereffects", "davinci", "handbrake"]


def _run(cmd, timeout=8):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           shell=False)
        return r.stdout + r.stderr
    except Exception:
        return ""


def probe_gpu():
    smi = shutil.which("nvidia-smi") or r"C:\WINDOWS\system32\nvidia-smi"
    out = _run([smi, "--query-gpu=utilization.gpu,memory.used,memory.total,"
                "utilization.memory", "--format=csv,noheader"], timeout=8)
    res = {"present": False}
    for line in out.splitlines():
        if "," in line:
            parts = [p.strip() for p in line.split(",")]
            try:
                res = {
                    "present": True,
                    "util_pct": int(parts[0].replace("%", "")),
                    "mem_used_mib": int(parts[1].replace("MiB", "")),
                    "mem_total_mib": int(parts[2].replace("MiB", "")),
                    "mem_util_pct": int(parts[3].replace("%", "")),
                }
            except (ValueError, IndexError):
                pass
    return res


def probe_cpu():
    # Use PowerShell Get-Counter (typeperf alternative) for a clean number
    ps = ("(Get-Counter '\\Processor(_Total)\\% Processor Time' -ErrorAction "
          "Stop).CounterSamples[0].CookedValue")
    out = _run(["powershell.exe", "-NoProfile", "-Command", ps], timeout=8)
    try:
        return round(float(out.strip().splitlines()[-1]), 1)
    except Exception:
        return None


def probe_processes():
    out = _run(["tasklist.exe", "/FO", "CSV"], timeout=8)
    names = []
    for line in out.splitlines()[1:]:
        # "name.exe","pid","session","session#","mem"
        try:
            name = line.split('","')[0].strip('"').lower()
            names.append(name)
        except Exception:
            pass
    return names


def verdict(gpu, cpu_pct, procs):
    procset = set(procs)
    streaming = any(p in procset for p in STREAM_EXES)
    gaming = any(any(g in p for g in GAME_EXES) for p in procset)
    gpu_heavy = any(any(g in p for g in GPU_HEAVY_EXES) for p in procset)

    state = "IDLE"
    if gaming:
        state = "GAMING"
    elif streaming:
        state = "STREAMING"
    elif gpu_heavy:
        state = "BUSY"
    elif gpu.get("present") and gpu["util_pct"] > 60:
        state = "BUSY"

    # safe_for_heavy: only when IDLE and GPU is cool and CPU is cool-ish
    safe = (state == "IDLE"
            and gpu.get("util_pct", 0) < 25
            and (cpu_pct is None or cpu_pct < 40))

    if state == "IDLE" and safe:
        max_cpu, max_gpu, max_par, can_gpu = 100, 100, 8, True
    elif state == "STREAMING":
        # never touch the encoder/GPU; keep CPU gentle
        max_cpu, max_gpu, max_par, can_gpu = 25, 0, 2, False
    elif state == "GAMING":
        max_cpu, max_gpu, max_par, can_gpu = 15, 0, 1, False
    else:  # BUSY
        max_cpu, max_gpu, max_par, can_gpu = 30, 15, 2, False

    return {
        "state": state,
        "safe_for_heavy": safe,
        "streaming": streaming,
        "gaming": gaming,
        "limits": {
            "max_cpu_pct": max_cpu,
            "max_gpu_pct": max_gpu,
            "max_parallel_jobs": max_par,
            "can_use_gpu": can_gpu,
        },
        "gpu": gpu,
        "cpu_pct": cpu_pct,
        "ts": time.time(),
    }


def snapshot():
    gpu = probe_gpu()
    cpu = probe_cpu()
    procs = probe_processes()
    return verdict(gpu, cpu, procs)


def main():
    v = snapshot()
    print(json.dumps(v, indent=2))


if __name__ == "__main__":
    main()

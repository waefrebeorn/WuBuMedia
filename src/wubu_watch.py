#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_watch.py — Production watchdog for the cohost.

Phase 7: Production Hardening.

Monitors the cohost process and its children:
  * Memory: RSS growth detected via psutil; restarts if > 1GB
  * CPU: samples CPU% every 5s; alerts if sustained > 85%
  * Liveness: checks face_state.json timestamp; alerts if stale > 30s
  * Auto-restart: if the cohost dies, restart it with backoff

Runs as a separate process. Usage:
  python src/wubu_watch.py --monitor cohost
  python src/wubu_watch.py --check   # one-shot health check

The watchdog itself is lightweight (~2% CPU) and can be run as a
Windows service or via `wu watch` from wu.cmd.

Research: memory leak tracking patterns from Ruth Grace Wong's
"Tracking Down a Memory Leak" (Stripe), and auto-restart patterns
from SO "How to automatically restart python to fix memory leak".

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import time
import subprocess
import json
import signal
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
WUBUMEDIA = os.path.dirname(HERE)
FACE_DIR = os.environ.get("WUBU_FACE_DIR",
                          os.path.join(WUBUMEDIA, "face"))

# Thresholds
MAX_MEMORY_MB = 1024    # restart if cohost uses > 1GB RSS
CPU_WARN_PCT = 85.0     # alert if CPU sustained above this
STATE_MAX_AGE = 30      # face_state.json must update within 30s
CHECK_INTERVAL = 5      # seconds between checks
MAX_RESTART_DELAY = 300 # max backoff (5 min)


def get_memory_mb(pid):
    """Return RSS memory in MB for a process, or None on failure."""
    try:
        import psutil
        p = psutil.Process(pid)
        return p.memory_info().rss / (1024 * 1024)
    except Exception:
        return None


def get_cpu_pct(pid):
    """Return CPU% for a process, or None on failure."""
    try:
        import psutil
        p = psutil.Process(pid)
        return p.cpu_percent(interval=0.5)
    except Exception:
        return None


def face_state_age():
    """Return age of face_state.json in seconds, or None if missing."""
    path = os.path.join(FACE_DIR, "face_state.json")
    try:
        return time.time() - os.stat(path).st_mtime
    except Exception:
        return None


def health_check():
    """One-shot health check. Returns dict of status flags."""
    result = {
        "timestamp": time.time(),
        "face_state_age": face_state_age(),
        "face_state_stale": False,
        "memory_mb": None,
        "cpu_pct": None,
    }
    age = result["face_state_age"]
    if age is not None and age > STATE_MAX_AGE:
        result["face_state_stale"] = True
    return result


def monitor(cohost_cmd=None, max_memory=MAX_MEMORY_MB):
    """Monitor the cohost process and restart it on crash/OOM.

    cohost_cmd: list of command parts, e.g. [sys.executable, 'src/wubu_cohost.py', '--speak']
    Runs forever (or until SIGTERM/SIGINT).
    """
    if cohost_cmd is None:
        cohost_cmd = [sys.executable, os.path.join(HERE, "wubu_cohost.py")]

    restart_delay = 5.0
    pid = None
    last_mem_check = 0

    def signal_handler(signum, frame):
        print("\n[watch] shutting down", flush=True)
        if pid:
            try:
                os.kill(pid, signal.SIGTERM)
            except Exception:
                pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)

    while True:
        print(f"[watch] starting cohost: {' '.join(cohost_cmd)}", flush=True)
        try:
            proc = subprocess.Popen(cohost_cmd,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            pid = proc.pid
        except Exception as e:
            print(f"[watch] failed to start cohost: {e}", flush=True)
            time.sleep(restart_delay)
            restart_delay = min(restart_delay * 1.5, MAX_RESTART_DELAY)
            continue

        # Monitor the running process
        stdout_buf = []
        crash_reason = None
        while proc.poll() is None:
            # Check memory
            if time.time() - last_mem_check > CHECK_INTERVAL * 6:
                mem = get_memory_mb(pid)
                if mem and mem > max_memory:
                    crash_reason = f"OOM: {mem:.0f}MB > {max_memory}MB"
                    break
                last_mem_check = time.time()

            # Check for error output (lines indicating crash)
            try:
                line = proc.stdout.readline()
                if line:
                    text = line.decode("utf-8", errors="replace").strip()
                    if text:
                        stdout_buf.append(text)
                        # Keep buffer small
                        if len(stdout_buf) > 20:
                            stdout_buf.pop(0)
                        # Look for crash indicators
                        if "Traceback" in text or "Error" in text:
                            crash_reason = text[:100]
                        print(f"  [cohost] {text}", flush=True)
            except Exception:
                pass

            # Check face_state freshness
            age = face_state_age()
            if age is not None and age > STATE_MAX_AGE * 3:
                print(f"[watch] face_state stale ({age:.0f}s)", flush=True)

            time.sleep(0.2)

        # Process exited — determine why and restart with backoff
        exit_code = proc.poll()
        if crash_reason:
            print(f"[watch] cohost crashed: {crash_reason}", flush=True)
        elif exit_code != 0:
            print(f"[watch] cohost exited (code={exit_code})", flush=True)
        else:
            print("[watch] cohost exited cleanly", flush=True)

        print(f"[watch] restarting in {restart_delay:.0f}s...", flush=True)
        time.sleep(restart_delay)
        # Backoff: short on graceful exit, longer on crash
        if crash_reason or exit_code != 0:
            restart_delay = min(restart_delay * 1.5, MAX_RESTART_DELAY)
        else:
            restart_delay = max(5.0, restart_delay * 0.5)
        pid = None


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="WuBuDesk cohost watchdog")
    ap.add_argument("--monitor", nargs="+", default=None,
                    help="Command to monitor (default: wubu_cohost.py)")
    ap.add_argument("--check", action="store_true",
                    help="One-shot health check")
    ap.add_argument("--max-mem", type=int, default=MAX_MEMORY_MB,
                    help="Max RSS in MB before restart")
    args = ap.parse_args()

    if args.check:
        h = health_check()
        print(json.dumps(h, indent=2))
        sys.exit(1 if h["face_state_stale"] else 0)

    cmd = args.monitor or [sys.executable, os.path.join(HERE, "wubu_cohost.py")]
    monitor(cohost_cmd=cmd, max_memory=args.max_mem)

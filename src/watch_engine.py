#!/usr/bin/env python3
"""watch_engine.py — WuBuDesk's pull-when-ready watch on wubuwizard.

Fetches wubuwizard, detects if the engine agent signaled DeepSeek-V4 readiness
(new commits touching gguf_reader.c / deepseek arch, or a READY_DEEPSEEK_V4
marker). Reports ONLY on a real signal so the boss isn't spammed.

Exit: prints a short status. Cron delivers it. No side effects (does not pull).
"""
import subprocess
import os

REPO = r"C:\Users\eman5\wubuwizard"

def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True,
                          cwd=REPO, timeout=120).stdout

def main():
    before = sh("git rev-parse HEAD").strip()
    sh("git fetch origin")
    branch = sh("git rev-parse --abbrev-ref HEAD").strip()
    upstream_ref = f"origin/{branch}"  # resolved Python var; avoids @{upstream} + f-string brace bug
    behind = sh(f"git rev-list --count HEAD..{upstream_ref}").strip()
    if behind == "" or behind == "0":
        print("WATCH: wubuwizard up to date (no new engine commits).")
        return
    # inspect new commits for the signal
    log = sh(f"git log --oneline HEAD..{upstream_ref}")
    marker = ("READY_DEEPSEEK_V4" in log) or ("deepseek4" in log.lower())
    touch = ("gguf_reader" in log) or ("deepseek" in log.lower())
    if marker or touch:
        print(f"WATCH SIGNAL: {behind} new engine commits. DeepSeek-V4 work likely ready.")
        print(log.strip()[:600])
        print("ACTION: WuBuDesk should `git pull` + re-port to Windows (sm_75) + verify load.")
    else:
        print(f"WATCH: {behind} new commits (unrelated to engine/deepseek). Logged.")

if __name__ == "__main__":
    main()

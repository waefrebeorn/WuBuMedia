# SPDX-License-Identifier: WaefreBeorn-UMV3
#!/usr/bin/env python3
"""
elevate.py — WuBuDesk's "run as Administrator" primitive (Windows).

Host is configured with UAC = Never Notify, so PowerShell
`Start-Process -Verb RunAs` auto-elevates with NO prompt. This gives the
Windows-side AGI silent admin when it needs it (install drivers, write to
protected paths, manage services, launch elevated builds).

Design:
  * Quoting-safe: the elevated command is serialized to JSON and passed to a
    generated .ps1 (never bash-interpolated into -Command).
  * Self-verifying: --test writes a proof file into C:\Windows (admin-only).
  * No secrets embedded; caller supplies the command.

Usage:
  python3 elevate.py -- <command> [args...]        # run elevated, wait
  python3 elevate.py --nowait -- <command> [args]  # launch elevated, detach
  python3 elevate.py --test                         # prove elevation works

Exit: the elevated process's exit code (0 if detached/ok).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

PS = r'''
$ErrorActionPreference = "Stop"
$cmd  = {cmd}
$args = {args}
$wait = {wait}
$marker = {marker}

function Write-Marker($c) {{
    if ($marker) {{ "$c" | Out-File -FilePath $marker -Encoding ascii }}
}}

try {{
    if ($wait -eq $true) {{
        $p = Start-Process -FilePath $cmd -ArgumentList $args -Verb RunAs -Wait -PassThru -NoNewWindow
        Write-Marker $p.ExitCode
    }} else {{
        Start-Process -FilePath $cmd -ArgumentList $args -Verb RunAs -NoNewWindow
        Write-Marker 0
    }}
}} catch {{
    Write-Marker -1
    throw
}}
'''


def run_elevated(command, args, wait=True):
    marker = os.path.join(tempfile.gettempdir(), "wubu_elevate_marker.txt")
    if os.path.exists(marker):
        os.unlink(marker)
    ps = PS.format(cmd=json.dumps(command), args=json.dumps(args),
                   wait="true" if wait else "false", marker=json.dumps(marker))
    with tempfile.NamedTemporaryFile("w", suffix=".ps1", delete=False) as f:
        f.write(ps)
        ps_path = f.name
    try:
        subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy",
                        "Bypass", "-File", ps_path], capture_output=True, text=True)
    finally:
        try:
            os.unlink(ps_path)
        except OSError:
            pass
    if os.path.exists(marker):
        with open(marker) as f:
            return int(f.read().strip() or 0)
    return 0


def self_test():
    marker = os.path.join(tempfile.gettempdir(), "wubu_elevate_marker.txt")
    if os.path.exists(marker):
        os.unlink(marker)
    target = r"C:\Windows\wubu_elevated_proof.txt"
    if os.path.exists(target):
        run_elevated("cmd.exe", ["/c", f"del /f /q {target}"], wait=True)
    run_elevated("cmd.exe", ["/c", f"echo ELEVATED_OK > {target} & whoami >> {target}"],
                 wait=True)
    ok = os.path.exists(target)
    print(f"proof_file_created: {ok}")
    if ok:
        with open(target) as f:
            print("proof_contents: " + " | ".join(l.strip() for l in f.read().splitlines()))
        # proof lives in C:\Windows (admin-owned) — only an elevated delete works
        run_elevated("cmd.exe", ["/c", f"del /f /q {target}"], wait=True)
        print("RESULT: ELEVATION WORKS (silent admin confirmed)")
    else:
        print("RESULT: elevation FAILED")
    return 0 if ok else 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--test", action="store_true")
    ap.add_argument("--nowait", action="store_true")
    ap.add_argument("rest", nargs="*")
    a = ap.parse_args()
    if a.test:
        return self_test()
    if a.rest and a.rest[0] == "--":
        a.rest = a.rest[1:]
    if not a.rest:
        print("usage: elevate.py [--test|--nowait] -- <command> [args]")
        return 1
    return run_elevated(a.rest[0], a.rest[1:], wait=not a.nowait)


if __name__ == "__main__":
    sys.exit(main())

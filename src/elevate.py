#!/usr/bin/env python3
"""elevate.py - run a command as Administrator (silent, UAC=Never Notify).

FIXED: the old template used Start-Process -Verb RunAs -NoNewWindow which
fails to launch elevated processes. Direct RunAs (no -NoNewWindow) is proven.
"""
import argparse, json, os, subprocess, sys, tempfile

PS = r'''
$cmd  = {cmd}
$cmdArgs = {args}
$wait = {wait}
$marker = {marker}
function Write-Marker($c) {{ if ($marker) {{ "$c" | Out-File -FilePath $marker -Encoding ascii }} }}
try {{
    if ($wait) {{
        $p = Start-Process -FilePath $cmd -ArgumentList $cmdArgs -Verb RunAs -Wait -PassThru
        Write-Marker $p.ExitCode
    }} else {{
        Start-Process -FilePath $cmd -ArgumentList $cmdArgs -Verb RunAs
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
    # PowerShell array syntax is @(...), not JSON's [...] — convert
    ps_args = "@(" + ", ".join(json.dumps(a) for a in args) + ")"
    ps = PS.format(cmd=json.dumps(command), args=ps_args,
                   wait="true" if wait else "false", marker=json.dumps(marker))
    with tempfile.NamedTemporaryFile("w", suffix=".ps1", delete=False) as f:
        f.write(ps)
        ps_path = f.name
    try:
        subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy",
                        "Bypass", "-File", ps_path], capture_output=True, text=True)
    finally:
        try: os.unlink(ps_path)
        except OSError: pass
    if os.path.exists(marker):
        with open(marker) as f:
            return int(f.read().strip() or 0)
    return 0

def self_test():
    target = r"C:\Windows\wubu_elevated_proof.txt"
    run_elevated("cmd.exe", ["/c", f"del /f /q {target}"], wait=True)
    if os.path.exists(target):
        print("FAILED: could not delete old proof - elevation broken")
        return 2
    run_elevated("cmd.exe", ["/c", f"echo ELEVATED_OK > {target} & whoami >> {target}"], wait=True)
    ok = os.path.exists(target)
    print(f"proof_file_created: {ok}")
    if ok:
        with open(target) as f:
            print("proof_contents: " + " | ".join(l.strip() for l in f.read().splitlines()))
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

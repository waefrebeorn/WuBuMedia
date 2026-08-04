#!/usr/bin/env python3
"""
wubu_desktop.py — native Windows desktop control for the cohost (no external deps).

Prefers wubucmd.exe (proper C11 Win32 tool, src/wubucmd.c) for window
enumeration + input; falls back to PowerShell for screen capture (PNG) and
UIAutomation clicks. This is the CUA "eyes + hands" on Windows without
third-party agents.

Capabilities:
  screenshot(path)     -> save PNG of primary screen
  click_dialog(title)  -> find a window by title and click a button by name
  type_text(text)      -> send keystrokes to the foreground window
  list_windows()       -> enumerate visible windows (for the cohost to "see")
  focus_window(substr) -> restore+foreground a window (native, via wubucmd)

Security: only acts locally; never exfiltrates. Driven by the agent, bounded by
BOUNDARIES.md (never touches drivers/kernel; respects stream GPU).
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import subprocess
import tempfile
import os

# Prefer the native C11 wubucmd.exe (faster, no PowerShell/COM spawn).
_HERE = os.path.dirname(os.path.abspath(__file__))
_WUBUCMD = os.path.join(_HERE, "wubucmd.exe")


def _run_wubucmd(args):
    """Run wubucmd.exe; return (rc, stdout). None if the exe is missing."""
    if not os.path.exists(_WUBUCMD):
        return None, ""
    try:
        r = subprocess.run([_WUBUCMD] + args, capture_output=True, text=True,
                           timeout=30)
        return r.returncode, r.stdout
    except Exception:
        return None, ""

PS = "powershell.exe"


def _ps(script):
    return subprocess.run([PS, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                          capture_output=True, text=True, timeout=60)


def screenshot(path=None):
    path = path or os.path.join(tempfile.gettempdir(), "wubu_desktop.png")
    script = f"""
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $screen.Size)
$bmp.Save('{path}'.Replace('/','\\\\'))
Write-Output "SAVED:$path"
"""
    r = _ps(script)
    if "SAVED" in r.stdout:
        return path
    raise RuntimeError(r.stderr or r.stdout)


def list_windows():
    # prefer the native C11 tool (no PowerShell/COM spawn)
    rc, out = _run_wubucmd(["list"])
    if rc == 0 and out.strip():
        wins = []
        for line in out.splitlines():
            # wubucmd prints "  [n] pid=NNNN  Title"
            s = line.strip()
            if s.startswith("[") and "]" in s:
                title = s.split("]", 1)[1].split("pid=", 1)[-1].strip()
                # strip leading "pid=NNNN  " if present
                if title.startswith("pid="):
                    title = title.split(None, 1)[-1] if "  " in title else ""
                if title:
                    wins.append(title)
        if wins:
            return wins
    # fallback: PowerShell + EnumWindows
    script = """
Add-Type @'
using System; using System.Runtime.InteropServices;
public class W { [DllImport("user32.dll")] public static extern bool EnumWindows(Func<IntPtr,int,bool> cb, int p);
[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h); }
'@
$out=@()
[W]::EnumWindows({ param($h,$p) if([W]::IsWindowVisible($h)){ $sb=New-Object System.Text.StringBuilder 256; [W]::GetWindowText($h,$sb,256) | Out-Null; if($sb.ToString().Trim().Length -gt 0){ $out += $sb.ToString() } }; return $true },0)
$out -join "`n"
"""
    r = _ps(script)
    return [w for w in r.stdout.splitlines() if w.strip()]


def focus_window(substr):
    """Restore + foreground a window whose title contains substr (native)."""
    rc, out = _run_wubucmd(["focus", substr])
    if rc == 0:
        return out.strip() or f"focus {substr}"
    # fallback: PowerShell SetForegroundWindow
    script = f"""
Add-Type @'
using System; using System.Runtime.InteropServices;
public class F {{ [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n); }}
'@
$sh=Add-Type -Name sh -MemberDefinition '[DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);' -PassThru
$h=[System.IntPtr]::Zero
# enumerate by title substring via EnumWindows
Add-Type @'
using System; using System.Runtime.InteropServices;
public class E {{ [DllImport("user32.dll")] public static extern bool EnumWindows(Func<IntPtr,int,bool> cb, int p);
[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n); }}
'@
$found=[System.IntPtr]::Zero
[E]::EnumWindows({{ param($h,$p) $sb=New-Object System.Text.StringBuilder 256; [E]::GetWindowText($h,$sb,256) | Out-Null; if($sb.ToString().Contains('{substr}')){{ $script:found=$h; return $false }}; return $true }},0) | Out-Null
if($found -ne [System.IntPtr]::Zero){{ [F]::ShowWindow($found,9); [F]::SetForegroundWindow($found); Write-Output "FOCUSED" }} else {{ Write-Output "NO_WINDOW" }}
"""
    r = _ps(script)
    return r.stdout.strip()


def click_dialog(title_substr, button_name="Normal"):
    """Find a window containing title_substr, then click a button with button_name.
    Used to dismiss OBS's safe-mode / crash-recovery prompt."""
    script = f"""
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$ae = [System.Windows.Automation.AutomationElement]::RootElement
$windows = $ae.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::True)
$found=$null
foreach($w in $windows){{ $n=$w.Current.Name; if($n -and $n.Contains('{title_substr}')){{ $found=$w; break }} }}
if(-not $found){{ Write-Output "NO_WINDOW"; exit }}
Write-Output ("WINDOW:" + $found.Current.Name)
$bcond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, '{button_name}')
$btn = $found.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $bcond)
if($btn){{ $p=$btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern); $p.Invoke(); Write-Output "CLICKED:{button_name}" }} else {{ Write-Output "NO_BUTTON:{button_name}" }}
"""
    r = _ps(script)
    return r.stdout.strip()


if __name__ == "__main__":
    print("windows:", list_windows()[:10])
    p = screenshot()
    print("screenshot:", p)

#!/usr/bin/env python3
"""register_host.py — register the WuBuDesk native messaging host in the
browser registry (Windows). Run elevated: python3 elevate.py -- python register_host.py

Registers under HKCU (per-user, no admin needed for HKCU) for both Chrome and Edge.
License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import sys
import winreg

HOST = "com.wubudesk.bridge"
MANIFEST = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "com.wubudesk.bridge.json")

# Read the manifest so path is absolute
with open(MANIFEST, encoding="utf-8") as f:
    m = json.load(f)
m["path"] = os.path.abspath(m["path"])
with open(MANIFEST, "w", encoding="utf-8") as f:
    json.dump(m, f, indent=2)

# Chrome + Edge look in the same per-user locations
locations = [
    (winreg.HKEY_CURRENT_USER, r"Software\Google\Chrome\NativeMessagingHosts"),
    (winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Edge\NativeMessagingHosts"),
]
for hkey, sub in locations:
    try:
        key = winreg.CreateKeyEx(hkey, os.path.join(sub, HOST), 0, winreg.KEY_WRITE)
        winreg.SetValueEx(key, None, 0, winreg.REG_SZ, MANIFEST)
        winreg.CloseKey(key)
        print("registered:", sub + "\\" + HOST)
    except Exception as e:
        print("ERR:", sub, e)

print("host manifest:", MANIFEST)

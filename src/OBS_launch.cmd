@echo off
rem WuBuDesk_OBS_launch.cmd - launch OBS from its own bin dir (fixes locale lookup)
cd /d "C:\Program Files\obs-studio\bin\64bit"
start "" obs64.exe

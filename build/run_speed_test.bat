@echo off
cd /d C:\Users\eman5\WuBuMedia
echo === Rebuild with weights loader ===
cc -Wall -Wextra -std=c11 -g -I src ^
  src\test_rvc_speed.c ^
  src\wubu_rvc.c ^
  src\wubu_rvc_parity.c ^
  src\wubu_rvc_weights.c ^
  src\wubu_vc.c ^
  src\wubu_rlm.c ^
  src\wubu_buddy.c ^
  -lsqlite3 -lm -o build\test_rvc_speed_dbg.exe 2>&1
echo BUILD_EXIT=%ERRORLEVEL%
if errorlevel 1 exit /b 1
echo === Run with Cartman model ===
build\test_rvc_speed_dbg.exe 2>&1
echo RUN_EXIT=%ERRORLEVEL%

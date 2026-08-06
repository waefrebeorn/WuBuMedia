@echo off
cd /d C:\Users\eman5\WuBuMedia
cc -Wall -std=c11 -g -I src ^
  src\test_load_model.c ^
  src\wubu_rvc.c ^
  src\wubu_rvc_parity.c ^
  src\wubu_rvc_weights.c ^
  src\wubu_rvc_kernels_exact.c ^
  src\wubu_rlm.c ^
  src\wubu_buddy.c ^
  -lsqlite3 -lm -o build\test_load_model.exe 2>&1
echo BUILD_EXIT=%ERRORLEVEL%
if errorlevel 1 exit /b 1
timeout /t 5 /nobreak >nul
build\test_load_model.exe 2>&1
echo RUN_EXIT=%ERRORLEVEL%

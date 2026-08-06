@echo off
rem ============================================================
rem  cuda_build.bat — compile WuBuMedia CUDA kernels for sm_75
rem  (RTX 2080 SUPER) using nvcc + MSVC host compiler.
rem
rem  Usage:  cmd.exe /c build\cuda_build.bat
rem  Exit:   0 = all OK, 1 = any failure
rem ============================================================
setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo CUDA_BUILD_FATAL: vcvars64.bat failed
    exit /b 1
)

set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64
set CUDA=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin
set PATH=%MSVC%;%CUDA%;%PATH%

set WUBU=C:\Users\eman5\WuBuMedia
set OUT=%WUBU%\build
if not exist "%OUT%" mkdir "%OUT%"

set NVCC=nvcc -arch=sm_75 -std=c++17 -O2 -I "%WUBU%\src"

call :compile "src\wubu_rvc_mono.cu"     wubu_rvc_mono
call :compile "src\wubu_rvc_kernels.cu"  wubu_rvc_kernels

echo CUDA_BUILD: ALL WUBUMEDIA KERNELS OK (sm_75, nvcc+MSVC)
exit /b 0

:compile
%NVCC% -c "%WUBU%\%~1" -o "%OUT%\%~2.o" >"%OUT%\%~2.log" 2>&1
if errorlevel 1 (
    echo [FAIL] %~1
    echo CUDA_BUILD: FAILED at %~1 — see build\%~2.log
    exit /b 1
)
echo [OK]   %~1
exit /b 0

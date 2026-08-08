@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin\nvcc.exe" -O3 -arch=sm_75 -Xcompiler /GS- -fmad=false -I C:\Users\eman5\WuBuMedia\src -c C:\Users\eman5\WuBuMedia\src\wubu_rvc_cuda.cu -o C:\Users\eman5\WuBuMedia\build\wubu_rvc_cuda.o
echo NVCC_RC=%ERRORLEVEL%

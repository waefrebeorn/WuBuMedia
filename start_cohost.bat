@echo off
REM start_cohost.bat — bring up the WuBuDesk cohost swarm on the Windows rig.
REM Launches: (1) llama-server brain (Qwen3.6-27B-IQ2_M :57064)
REM           (2) llama-server eyes (Qwen3.5-9B-VL + mmproj :57065)
REM           (3) the overlay HTTP server (face :8137)
REM           (4) the perceive->think->speak loop (conversational)
REM SPDX-License-Identifier: WaefreBeorn-UMV3

setlocal
set ROOT=C:\Users\eman5\WuBuMedia
set LLAMA=D:\llama.cpp\llama-server.exe
set CUDABIN=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin
set PATH=%CUDABIN%;%PATH%

echo [WuBuDesk] launching brain (Qwen3.6-27B-IQ2_M :57064)...
start "wubu-brain" cmd /k "%LLAMA% -m D:/models/Qwen3.6-27B-GGUF/Qwen3.6-27B-UD-IQ2_M.gguf --host 127.0.0.1 --port 57064 -ngl 40 -fa -c 8192 --no-warmup"

echo [WuBuDesk] launching eyes (Qwen3.5-9B-VL + mmproj :57065)...
start "wubu-eyes" cmd /k "%LLAMA% -m D:/models/Qwen3.5-9B-VL/Qwen3.5-9B-UD-Q4_K_XL.gguf --mmproj D:/models/Qwen3.5-9B-VL/mmproj-F16.gguf --host 127.0.0.1 --port 57065 -ngl 99 -fa -c 8192 --no-warmup"

echo [WuBuDesk] launching overlay HTTP (:8137)...
cd /d %ROOT%
start "wubu-face" cmd /k "python wubu_obs.py"

echo [WuBuDesk] launching cohost loop (conversational)...
start "wubu-loop" cmd /k "python src\wubudesk_loop.py --loop 15 --max 999999 --speak --conversational"

echo [WuBuDesk] swarm up. Boss, I am listening.
endlocal

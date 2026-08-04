@echo off
REM start_cohost.bat — bring up the WuBuDesk cohost swarm on the Windows rig.
REM Launches: (1) llama-server brain (multimodal, :57064)  [skipped if already up]
REM           (2) the overlay HTTP server (face :8137)
REM           (3) the secure browser bridge (WS :18765)
REM           (4) the perceive->think->speak loop (conversational)
REM SPDX-License-Identifier: WaefreBeorn-UMV3
REM
REM NOTE: the brain at :57064 and the overlay at :8137 are often already live
REM (started by Hermes background processes / prior sessions). We probe each
REM port and only launch what is missing, so re-running this file is safe.
REM
REM INTERPRETER NOTE: .venv_win is a venv clone that resolves to the Hermes
REM agent Python, so the cohost deps (kokoro/silero/whisper/sounddevice/
REM websocket) live in the Hermes agent site-packages. We point PYTHONPATH
REM there and launch every cohost process with the .venv_win interpreter so
REM the imports resolve regardless of the ambient shell environment.

setlocal
set ROOT=C:\Users\eman5\WuBuMedia
set PY=C:\Users\eman5\WuBuMedia\.venv_win\Scripts\python.exe
set HERMES_SITE=C:\Users\eman5\AppData\Local\hermes\hermes-agent\venv\Lib\site-packages
set PYTHONPATH=%HERMES_SITE%;%PYTHONPATH%
set LLAMA=D:\llama.cpp\llama-server.exe
set CUDABIN=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin
set PATH=%CUDABIN%;%PATH%

REM --- brain (multimodal :57064) ---
curl -s -o nul -w "%%{http_code}" --max-time 3 http://127.0.0.1:57064/v1/models > nul 2>&1
if errorlevel 1 (
  echo [WuBuDesk] launching brain (multimodal :57064)...
  start "wubu-brain" cmd /k "%LLAMA% -m D:/models/Qwen3.6-27B-GGUF/Qwen3.6-27B-UD-IQ2_M.gguf --host 127.0.0.1 --port 57064 -ngl 40 -fa -c 8192 --no-warmup"
) else (
  echo [WuBuDesk] brain already up on :57064, skipping
)

REM --- overlay HTTP server (face :8137) ---
curl -s -o nul -w "%%{http_code}" --max-time 3 http://127.0.0.1:8137/index.html > nul 2>&1
if errorlevel 1 (
  echo [WuBuDesk] launching overlay HTTP (:8137)...
  cd /d %ROOT%
  start "wubu-face" cmd /k "%PY% -m http.server 8137 --bind 127.0.0.1 --directory %ROOT%\face"
) else (
  echo [WuBuDesk] overlay already up on :8137, skipping
)

REM --- secure browser bridge (WS :18765) ---
curl -s -o nul -w "%%{http_code}" --max-time 3 http://127.0.0.1:18765/ > nul 2>&1
if errorlevel 1 (
  echo [WuBuDesk] launching browser bridge (WS :18765)...
  cd /d %ROOT%
  start "wubu-bridge" cmd /k "%PY% browser\wubu_bridge.py"
) else (
  echo [WuBuDesk] bridge already up on :18765, skipping
)

REM --- cohost loop (conversational) ---
echo [WuBuDesk] launching cohost loop (conversational)...
cd /d %ROOT%
start "wubu-loop" cmd /k "%PY% src\wubudesk_loop.py --loop 15 --max 999999 --speak --conversational"

echo [WuBuDesk] swarm up. Boss, I am listening.
endlocal

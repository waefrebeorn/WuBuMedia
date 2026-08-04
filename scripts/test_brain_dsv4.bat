@echo off
REM test_brain_dsv4.bat — test-load DeepSeek-V4-Flash-ConfigI on :57066
REM Verifies whether this 96GB model can serve on the 67GB-RAM / 8GB-VRAM rig.
REM Does NOT touch the live brain @:57064. mmap + light GPU offload + small ctx.
setlocal
set MODEL=D:\models\DeepSeek-V4-Flash-ConfigI\DeepSeek-V4-Flash-0731-ConfigI-00001-of-00003.gguf
set LLAMA=D:\llama.cpp\llama-server.exe
"%LLAMA%" -m "%MODEL%" --mmap -ngl 10 -c 2048 --port 57066 --host 127.0.0.1
endlocal

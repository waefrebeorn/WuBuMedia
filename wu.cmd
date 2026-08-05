@echo off
REM WuBuDesk quick launcher - "wu <command>" launches cohost actions
REM Runs from the WuBuMedia venv python (.venv_win or system python3)
setlocal

set ROOT=%~dp0
set SRC=%ROOT%src
set FACE_DIR=%ROOT%face
set PYTHON=%ROOT%.venv_win\Scripts\python.exe

if not exist "%PYTHON%" set PYTHON=python3

if "%~1"=="" (
    echo WuBuDesk launcher. Usage: wu ^<command^>
    echo Commands:
    echo   wu speak ^<mood^> ^<text^>   - speak on cohost (e.g., wu speak happy "yo chat")
    echo   wu mood ^<mood^>              - set cohost mood only
    echo   wu stop                      - stop speaking
    echo   wu capture                   - optimize PS5/Monster capture card for low latency
    echo   wu capture --list            - list capture device sources
    echo   wu capture --test-format     - test MJPEG vs YUY2 FPS on capture card
    echo   wu status                    - show cohost status
    exit /b 0
)

set CMD=%~1
shift

if "%CMD%"=="speak" (
    "%PYTHON%" "%SRC%\wubu_obs.py" speak --mood "%~1" --text "%~2" --mode live
    goto :eof
)

if "%CMD%"=="say" (
    "%PYTHON%" "%SRC%\wubu_obs.py" speak --mood "%~1" --text "%~2" --mode live
    goto :eof
)

if "%CMD%"=="mood" (
    "%PYTHON%" "%SRC%\wubu_obs.py" speak --mood "%~1" --text "" --mode live
    goto :eof
)

if "%CMD%"=="stop" (
    "%PYTHON%" "%SRC%\wubu_obs.py" speak --mood "happy" --text "" --mode live
    goto :eof
)

if "%CMD%"=="capture" (
    "%PYTHON%" "%SRC%\wubu_capture.py" %*
    goto :eof
)

if "%CMD%"=="wss" (
    "%PYTHON%" "%SRC%\wubu_wss.py" %*
    goto :eof
)

if "%CMD%"=="face" (
    "%PYTHON%" "%SRC%\wubu_face.py" %*
    goto :eof
)

if "%CMD%"=="status" (
    "%PYTHON%" "%SRC%\wubu_obs.py" status
    goto :eof
)

echo Unknown command: %CMD%

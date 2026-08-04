@echo off
REM Build Box3D lib, then link wubu_world.c into wubu_world.exe and run it.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set BOX3D=D:\engines\box3d
set OUT=D:\engines\box3d\build
if not exist %OUT% mkdir %OUT%
cd /d %OUT%
cmake -S %BOX3D% -B %OUT% > build_log.txt 2>&1
cmake --build %OUT% --config Release >> build_log.txt 2>&1
echo BUILD_DONE >> build_log.txt
REM link our shim
cl.exe /std:c11 /I %BOX3D%\include C:\Users\eman5\WuBuMedia\src\wubu_world.c /link /LIBPATH:%OUT%\src\Release box3d.lib /Fe:C:\Users\eman5\WuBuMedia\wubu_world.exe >> build_log.txt 2>&1
echo LINK_DONE >> build_log.txt
if exist C:\Users\eman5\WuBuMedia\wubu_world.exe ( echo EXE_OK >> build_log.txt ) else ( echo EXE_FAIL >> build_log.txt )
type build_log.txt

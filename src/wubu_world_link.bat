@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl.exe /MT src\wubu_world.c /I D:\engines\box3d\include /link /LIBPATH:D:\engines\box3d\build\src\Release box3d.lib /Fe:wubu_world.exe > link_log.txt 2>&1
if exist wubu_world.exe ( echo EXE_OK >> link_log.txt ) else ( echo EXE_FAIL >> link_log.txt )
type link_log.txt

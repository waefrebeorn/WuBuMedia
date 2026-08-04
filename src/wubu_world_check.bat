@echo off
REM syntax-check wubu_world.c against real Box3D headers, write result to build_log.txt
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set BOX3D=D:\engines\box3d
cl.exe /c /std:c11 /I "%BOX3D%\include" src\wubu_world.c /Fowubu_world.obj > build_log.txt 2>&1
if exist wubu_world.obj ( echo SYNTAX_OK >> build_log.txt ) else ( echo SYNTAX_FAIL >> build_log.txt )
type build_log.txt

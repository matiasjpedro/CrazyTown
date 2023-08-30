@echo off

setlocal enabledelayedexpansion

set CommonCompilerFlags= -MTd -nologo -EHa- -Gm- -GR- -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -wd4211 -wd4075 -ZI -FC /DEBUG
set CommonLinkerFlags= -incremental:NO /IGNORE:4075 

if "%~1"=="dev" (
	set "CommonCompilerFlags=!CommonCompilerFlags! /DDEVELOPMENT=1 -Od"
) else (
	set "CommonCompilerFlags=!CommonCompilerFlags! -O2"
)

mkdir ..\build
pushd ..\build 

cd. > building_marker

cl  %CommonCompilerFlags% %ConfigDefines% ..\src\CrazyTownApp.cpp /LD /link %CommonLinkerFlags% /EXPORT:AppPreUpdate /EXPORT:AppUpdate /EXPORT:AppInit /EXPORT:AppShutdown /EXPORT:AppOnHotReload /EXPORT:AppOnDrop 
cl  %CommonCompilerFlags% %ConfigDefines% ..\src\CrazyTownWin32.cpp /link %CommonLinkerFlags% user32.lib d3d11.lib d3dcompiler.lib winmm.lib Shell32.lib ../data/app_icon.res

del building_marker

popd

rem This is in case we want to generate new pdbs in case VS lock those,
rem As I'm using remedy this is not required
rem /PDB:CrazyTownApp%random%.pdb
rem del *.pdb
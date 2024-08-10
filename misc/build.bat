@echo off

rem MSVC environment setup ---------------------------------------------------------------------

rem set vcvars64path="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64"

cl >nul 2>&1

if %errorlevel% neq 0 (
	if defined vcvars64path (
    	call %vcvars64path%
	) else (
		echo You're executing this bat without setting the MSVC environment first.
		echo Uncomment vcvars64path and set the proper path.
		pause ul
		exit /b 
	)
) 


rem BUILD setup ------------------------------------------------------------------------------


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

cl  %CommonCompilerFlags% ..\src\CrazyTownApp.cpp /LD /link %CommonLinkerFlags% /EXPORT:AppPreUpdate /EXPORT:AppUpdate /EXPORT:AppPreInit /EXPORT:AppInit /EXPORT:AppShutdown /EXPORT:AppOnHotReload /EXPORT:AppOnDrop  
cl  %CommonCompilerFlags% /DAPPNAME=\"CrazyTownApp\" ..\src\CrazyTownWin32.cpp /link %CommonLinkerFlags% user32.lib d3d11.lib d3dcompiler.lib winmm.lib Shell32.lib ../data/app_icon.res

del building_marker

popd

rem This is in case we want to generate new pdbs in case VS lock those,
rem As I'm using remedy this is not required
rem /PDB:CrazyTownApp%random%.pdb
rem del *.pdb
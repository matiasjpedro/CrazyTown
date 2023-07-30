@echo off
set CommonCompilerFlags= -MTd -nologo -EHa- -Gm- -GR- -Od -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -wd4211 -wd4075 -ZI -FC /DEBUG
set CommonLinkerFlags= -incremental:NO /IGNORE:4075 

mkdir ..\build
pushd ..\build 

rem /PDB:CrazyTownHotReload%random%.pdb
rem del *.pdb

cd. > building_marker

cl  %CommonCompilerFlags% ..\src\CrazyTownApp.cpp /LD /link %CommonLinkerFlags% /EXPORT:AppUpdate /EXPORT:AppInit /EXPORT:AppShutdown /EXPORT:AppOnHotReload /EXPORT:AppOnDrop 
cl  %CommonCompilerFlags% ..\src\CrazyTownWin32.cpp /link %CommonLinkerFlags% user32.lib d3d11.lib d3dcompiler.lib winmm.lib Shell32.lib 

del building_marker

popd
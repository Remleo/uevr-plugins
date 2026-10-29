@echo off
rem Builds SpritePlane.dll, a UEVR plugin, with the MSVC x64 toolchain. It uses UEVR's plugin API headers and links the
rem safetyhook and Zydis libraries UEVR's own build produced; the static CRT matches theirs and keeps the DLL free of
rem runtime dependencies.
setlocal
call "D:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set DEPS=D:\Game Dev\UEVR\build\_deps
if not exist build mkdir build
cl /nologo /std:c++latest /O2 /MT /EHsc /W3 /LD /I "D:\Game Dev\UEVR\include" /I "%DEPS%\safetyhook-src\include" ^
   src\Plugin.cpp src\Find.cpp src\Basis.cpp src\Offsets.cpp src\View.cpp src\Memory.cpp /Fobuild\ /Fdbuild\ ^
   /link /OUT:build\SpritePlane.dll /PDB:build\SpritePlane.pdb /IMPLIB:build\SpritePlane.lib ^
   "%DEPS%\safetyhook-build\Release\safetyhook.lib" "%DEPS%\zydis-build\Release\Zydis.lib" "%DEPS%\zydis-build\zycore\Release\Zycore.lib"
exit /b %errorlevel%

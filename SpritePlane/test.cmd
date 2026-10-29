@echo off
rem Builds and runs the desktop test of SpritePlane (test\test.cpp): its arithmetic and what it does to one view, with
rem no game and no UEVR.
setlocal
call "D:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build\test mkdir build\test
cl /nologo /std:c++latest /O2 /MT /EHsc /W3 test\test.cpp src\Basis.cpp src\Offsets.cpp src\View.cpp src\Memory.cpp ^
   /Fobuild\test\ /Fdbuild\test\ /Fe:build\test\test.exe || exit /b 1
build\test\test.exe
exit /b %errorlevel%

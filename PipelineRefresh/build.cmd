@echo off
rem Builds PipelineRefresh.dll, a UEVR plugin, with the MSVC x64 toolchain. Only UEVR's plugin API headers are used;
rem the static CRT keeps the DLL free of runtime dependencies.
setlocal
call "D:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /LD /I "D:\Game Dev\UEVR\include" src\Plugin.cpp src\Rebind.cpp /Fobuild\ /Fdbuild\ /link /OUT:build\PipelineRefresh.dll /PDB:build\PipelineRefresh.pdb /IMPLIB:build\PipelineRefresh.lib
exit /b %errorlevel%

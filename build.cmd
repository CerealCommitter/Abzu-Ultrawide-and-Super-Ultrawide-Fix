@echo off
rem Builds bin\version.dll (the ABZU fix) with the MSVC x64 toolset.
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo MSVC x64 tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist bin mkdir bin
if not exist obj mkdir obj
cl /nologo /O2 /MT /W3 /EHsc /GS- /DUNICODE /D_UNICODE /Foobj\ ^
   src\AbzuFix.cpp src\Settings.cpp src\proxy_version.cpp ^
   /LD /Febin\version.dll ^
   /link /DEF:src\version.def /NODEFAULTLIB:libcmtd user32.lib kernel32.lib gdi32.lib shell32.lib || exit /b 1
echo Built bin\version.dll

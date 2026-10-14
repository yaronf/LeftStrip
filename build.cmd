@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this from an x64 Native Tools Command Prompt for Visual Studio 2022.
  exit /b 1
)
pushd "%~dp0"
cl /nologo /O2 /W4 /permissive- /utf-8 /std:c++17 /MT /EHsc LeftStrip.cpp /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib dwmapi.lib gdi32.lib /OUT:LeftStrip.exe
set "result=%errorlevel%"
if exist LeftStrip.obj del LeftStrip.obj
popd
exit /b %result%

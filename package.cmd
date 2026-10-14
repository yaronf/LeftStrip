@echo off
setlocal
set "ROOT=%~dp0"
pushd "%ROOT%"

where cl >nul 2>nul
if errorlevel 1 (
  echo Run this from an x64 Native Tools Command Prompt for Visual Studio 2022.
  exit /b 1
)

call "%ROOT%build.cmd"
if errorlevel 1 (
  popd
  exit /b 1
)

set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
  where iscc >nul 2>nul && for /f "delims=" %%I in ('where iscc') do set "ISCC=%%I"
)
if not defined ISCC (
  echo Inno Setup 6 not found. Install from https://jrsoftware.org/isinfo.php
  echo or: winget install JRSoftware.InnoSetup
  popd
  exit /b 1
)

if not exist "%ROOT%dist" mkdir "%ROOT%dist"
"%ISCC%" "%ROOT%installer\LeftStrip.iss"
set "result=%errorlevel%"
popd
exit /b %result%

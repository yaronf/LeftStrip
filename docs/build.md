# Build & test

Needs Visual Studio 2022 / Build Tools (**Desktop development with C++**), MSVC, Windows 11 SDK. CMake 3.20+ optional. No third-party libraries.

Static CRT (`/MT`). Output is unsigned x64.

## build.cmd

From an **x64 Native Tools Command Prompt for VS 2022**:

```bat
build.cmd
```

Writes `LeftStrip.exe` in the repo root.

## Installer

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`):

```bat
package.cmd
```

Output: `dist\LeftStrip-Setup-1.0.2.exe`.

## Version

About reads the product version from the executable. On a release, set the same version in:

- `LeftStrip.rc` (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`)
- `LeftStrip.manifest` (`assemblyIdentity version`)
- `installer/LeftStrip.iss` (`MyAppVersion`)

## CMake

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run `build\Release\LeftStrip.exe`. CMake may overwrite the ini beside that output on rebuild — back up settings if needed. ARM64: `-A ARM64` (not validated here). Windows on ARM can run the x64 build via emulation.

## Integration test

See also [VALIDATION.md](../VALIDATION.md). `tests/IntegrationTest.cpp` reserves real desktop geometry — use a disposable session. It rewrites the ini beside the exe you pass it; never point it at your daily copy.

```powershell
cmake -S tests -B test-build -G "Visual Studio 17 2022" -A x64
cmake --build test-build --config Release
# Copy LeftStrip.exe + LeftStrip.ini into a separate test folder first.
.\test-build\Release\IntegrationTest.exe C:\absolute\test-folder\LeftStrip.exe
```

Needs a normal interactive desktop (Explorer AppBar registration). Sandboxes often fail `ABM_NEW`.

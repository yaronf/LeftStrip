# LeftStrip for Windows 11

A small native C++/Win32 tray utility. It reserves a configurable left strip of the primary monitor using the Windows Shell AppBar API and moves one existing top-level application window into it. Default width: 20% of the full monitor width. No .NET, Electron, installer, injected DLL, or background service is required. The target stays a separate application window.

## Quick start

1. Extract the entire ZIP into a writable permanent folder, such as Documents\LeftStrip. Keep LeftStrip.exe and LeftStrip.ini together.
2. Open the application you want in the strip, then run LeftStrip.exe normally.
3. Right-click the LeftStrip icon in the notification area (possibly under the hidden-icons arrow), or right-click the empty strip. Choose **Select existing window**, then the desired window.
4. Use **Strip width** for presets, or **Edit configuration** followed by **Reload configuration** for a custom percentage.
5. Select **Start at sign-in** if desired. Its check mark means a per-user Run entry exists; Windows Startup Apps settings can separately disable execution.
6. Use **Exit and restore** to release the reservation and restore the target's saved placement and topmost state. **Pause and release space** does the same while leaving the tray utility running; resume reacquires the target.

The strip remains reserved while the target is closed, minimized, or absent. A minimized target is left minimized. Another ordinary window maximized on the primary monitor uses the remaining work area. Ordinary floating windows can still overlap the strip. With **Keep target on top** off (the default), manually bring the target forward if necessary.

The target app must already be running. LeftStrip never launches it. Start the target separately using its own startup option if needed. LeftStrip waits and attaches when exactly one window matches the saved filters. Multiple matches cause it to wait rather than pick arbitrarily. The live selection remains attached when its title changes; reacquisition after a restart uses the saved filters.

## Configuration

LeftStrip.ini is beside the executable. The bundled file is UTF-16 LE so saved non-English window titles are preserved. Menu selections write settings immediately. Changes made manually apply after **Reload configuration** or a restart. Save the file before selecting Reload. Keep the folder writable and avoid Program Files.

```ini
[LeftStrip]
WidthPercent=20
ProcessName=YourDashboard.exe
TitleContains=Dashboard
WindowClass=
PollMilliseconds=1000
AlwaysOnTop=0
RestoreOnExit=1
```

- WidthPercent: integer 5..50, percentage of the full primary monitor width; outside values are clamped. Windows negotiates the final rectangle around existing taskbars/AppBars.
- ProcessName: executable basename including .exe, case-insensitive; e.g. electron.exe or the packaged application's actual executable name. Not a path.
- TitleContains: case-insensitive literal title substring, no regex or wildcard.
- WindowClass: exact case-insensitive Win32 class name, optional. Electron often uses Chrome_WidgetWin_1; use the picker to discover it.
- Filters combine with AND; empty filters are ignored. If all three are empty, no automatic attachment occurs.
- PollMilliseconds: 250..10000, default 1000. Each tick corrects placement without deliberately activating the target. It is periodic enforcement, so a user/app move may remain visible until the next tick.
- AlwaysOnTop: 0/1. Optional topmost target; the appbar background itself stays at the bottom. Fullscreen notification temporarily drops topmost added by LeftStrip. A target that was already topmost retains its own policy.
- RestoreOnExit: 1 restores saved window placement on exit, pause, reload or target switching; 0 leaves its current placement. Neither option closes the application.

The picker saves the executable basename, full current title as TitleContains, and window class. Shorten TitleContains to a stable distinctive fragment if titles contain filenames, counters or live data. A PID/HWND is intentionally not persisted because both change and can be reused. A process handle helps avoid restoring a newly created process that reused a previous PID.

## Startup, stopping and removal

**Start at sign-in** toggles this entry, with no administrator rights:

HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
Value: LeftStrip
Data: quoted absolute path to LeftStrip.exe

It starts after interactive sign-in when Windows processes startup applications, not before login. Windows may delay it. Only one instance runs per interactive session. If you move the folder, disable startup first, move it, then enable startup from the new copy. Do not run multiple copies with different configurations in the same session.

To stop from a terminal:

```powershell
.\LeftStrip.exe --quit
```

This asks the existing instance to exit; it does not create an AppBar. Exit and normal session shutdown send ABM_REMOVE before destroying the AppBar window. Exit restores only the managed target; Windows controls how other apps respond to work-area changes. Explorer restart is handled by recreating the tray icon and registering the AppBar again.

To uninstall: uncheck **Start at sign-in**, choose **Exit and restore**, then delete the extracted folder. If the executable is already missing, delete only the LeftStrip value at the registry location above (or use Windows Startup Apps to disable it). No service, task, driver, or other installation footprint is created.

Force termination, a crash or power loss cannot execute cleanup or restore the target. The shell normally discards a dead AppBar; if an abnormal exit leaves stale space, restart Windows Explorer through Task Manager, or sign out and back in. This program deliberately does not overwrite the global work area with SPI_SETWORKAREA, which could interfere with other taskbars/AppBars.

## Build from source

Install Visual Studio 2022 / Build Tools with **Desktop development with C++**, MSVC, and a Windows 11 SDK. CMake 3.20+ is optional. No third-party libraries or network access during build.

Option A: open an **x64 Native Tools Command Prompt for VS 2022**, enter the extracted folder, run:

```bat
build.cmd
```

This writes LeftStrip.exe next to the configuration. The Microsoft C++ runtime is linked statically (/MT).

Option B: from a terminal with CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run build\Release\LeftStrip.exe; CMake copies the default INI beside it. Rebuilding may copy the source INI over a changed runtime INI, so back up settings before rebuilding in place. For a native Windows ARM64 build use -A ARM64 with the corresponding VS tools installed. The included executable is x64, unsigned, with a statically linked C++ runtime. Windows on ARM can run it through x64 emulation. ARM64 output has not been validated here.

## OS and application limits

- An AppBar changes the shell work area; it is not a second virtual monitor or an absolute exclusion boundary. Maximized windows normally honor it, but freely moved windows, custom maximize implementations, games, exclusive/borderless fullscreen, and apps that explicitly use monitor bounds can overlap it. The utility does not intercept every application's window messages or redefine Snap zones.
- The left edge is negotiated with the shell. An existing taskbar/AppBar on that edge may occupy the outermost area. Top/bottom taskbars reduce the strip's height. Existing AppBars have to coexist; the exact outermost 20% cannot be promised in a conflicting setup.
- A target can refuse sizing, enforce a minimum width, handle WM_GETMINMAXINFO, or repeatedly reposition itself. Electron's minimum window width is an application setting. If its minimum exceeds the strip, increase the strip width or change that app's settings/source. LeftStrip cannot crop its contents, make its UI responsive, or guarantee containment.
- Elevated/protected applications may reject access or positioning from a normal process. Use the same elevation level only if the chosen target requires it. LeftStrip neither elevates itself nor bypasses UIPI. Per-user sign-in startup does not grant elevation.
- Visible, unowned top-level windows with titles are eligible. Dialogs, tool windows and cloaked windows are excluded. Some Store/UWP/hosted apps and unusual window hierarchies do not expose a suitable controllable window. Windows 11 Notepad may be hosted differently from classic Notepad.
- The primary monitor alone is supported. Display/DPI/AppBar changes trigger renegotiation; the process requests per-monitor DPI awareness v2 and accounts for invisible DWM frame margins. Unusual mixed-DPI target apps, hot-plug arrangements and virtual desktop transitions need manual verification on your setup. The utility is session-wide, not specific to one virtual desktop.
- The title bar and app decorations remain. No reparenting, style stripping, plugin or modification to the target is used. Optional topmost can cover other windows; fullscreen behavior still depends on shell notifications and the target's own behavior.
- Changes to other apps' layouts are controlled by Windows. Exact recovery of all other windows after removing the reservation is not promised.
- The binary is unsigned. Windows reputation/security controls may prompt or block it. Source and build instructions are included for local compilation.

Microsoft API references:
- https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars
- https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shappbarmessage
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowplacement
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos

## Validation

See VALIDATION.md for results and remaining manual checks. tests/IntegrationTest.cpp creates its own target, launches an isolated copy of the utility, and checks real shell work-area reservation, target frame placement, work-area restoration, target restoration and clean exit. It temporarily changes desktop geometry: run only in a disposable desktop/session. It writes filters into the INI beside the executable passed to it, so never point it at your configured production copy.

```powershell
cmake -S tests -B test-build -G "Visual Studio 17 2022" -A x64
cmake --build test-build --config Release
# First copy LeftStrip.exe and LeftStrip.ini into a separate test folder.
.\test-build\Release\IntegrationTest.exe C:\absolute\test-folder\LeftStrip.exe
```

A restricted sandbox may block communication with Explorer and cause ABM_NEW to fail. The integration test requires normal interactive desktop access.

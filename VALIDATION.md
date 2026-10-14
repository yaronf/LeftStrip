Validation performed 2026-10-14

Build: Visual Studio 2022, MSVC 19.44.35228, Windows SDK 10.0.26100.0,
Release x64, C++17, /W4, static Microsoft C++ runtime. No compiler warnings.
Host reported Windows build 10.0.26200; compiler host ARM64, output x64.

Actual interactive-shell integration test (normal desktop access):
- Primary work area before: (0,0)-(1920,1032)
- AppBar rectangle: (0,0)-(384,1032)
- Work area while running: (384,0)-(1920,1032)
- Managed target visible frame: (0,0)-(384,1032)
- Reservation: PASS
- Target placement: PASS
- Work area restored on exit: PASS
- Original target window restored on exit: PASS
- Clean process exit: PASS

An initial sandboxed check could not register an AppBar. Running the check
with ordinary shell access allowed integration testing. A workspace-coordinate
restoration issue found during the test was fixed and the full check rerun.

The target was a native test window, not Electron. Remaining manual coverage:
tray menu interaction and configuration persistence, sign-in startup, pause/resume,
Explorer restart, target exit/relaunch, changing titles, ambiguous filters,
elevated targets, Electron minimum sizes, fullscreen, mixed DPI, monitor hotplug,
and multiple existing AppBars. These behaviors are implemented but not all have
been exercised end to end on this host. No claim of exhaustive OS compatibility.

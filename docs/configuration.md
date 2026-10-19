# Configuration

`LeftStrip.ini` is UTF-16 LE (non-English titles stay intact). Menu actions save immediately. Manual edits need **Reload configuration** or a restart.

## Where the file lives

| Mode | Path |
|---|---|
| Portable | `LeftStrip.ini` beside the exe (folder must be writable) |
| Installed | `%AppData%\LeftStrip\LeftStrip.ini` (seeded from `LeftStrip.ini.default` on first run) |

If an ini exists beside the exe, that file wins.

## Keys

```ini
[LeftStrip]
WidthPercent=20
ProcessName=YourDashboard.exe
TitleContains=Dashboard
WindowClass=
PollMilliseconds=1000
AlwaysOnTop=1
RestoreOnExit=1
```

| Key | Meaning |
|---|---|
| `WidthPercent` | 5–50; percent of primary monitor width (clamped). Shell negotiates around other AppBars/taskbars. |
| `ProcessName` | Executable basename only (e.g. `electron.exe`), case-insensitive. |
| `TitleContains` | Case-insensitive literal substring (no regex/wildcards). |
| `WindowClass` | Exact class name, optional (Electron often `Chrome_WidgetWin_1`). |
| `PollMilliseconds` | 250–10000; placement is re-applied on this interval. |
| `AlwaysOnTop` | `0`/`1`. Default `1`: keep the target on top. |
| `RestoreOnExit` | `1` restores saved placement on exit/pause/reload/switch; `0` leaves it. Never closes the app. |

Filters combine with **AND**; empty ones are ignored. If all are empty, nothing attaches automatically.

The window picker stores basename, current title, and class. Shorten `TitleContains` if titles include filenames or live counters. PID/HWND are not persisted.

## Startup and quit

**Start at sign-in** toggles:

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` → value `LeftStrip` → quoted path to the exe.

One instance per interactive session. After moving the folder, turn startup off, move, then enable again from the new copy.

```powershell
.\LeftStrip.exe --quit
```

Asks the running instance to exit (no AppBar created). Uninstall (Setup) does the same before removing files.

**Portable remove:** disable startup → **Exit and restore** → delete the folder.  
**Setup remove:** Apps → Installed apps (or Start Menu uninstall). `%AppData%\LeftStrip` is left alone.

## Behavior notes

- Strip stays reserved if the target is closed, minimized, or missing.
- Maximized windows on the primary monitor use the remaining work area; floating windows can still overlap the strip.
- After a crash/kill, restart Explorer or sign out if leftover reserved space remains. LeftStrip does not call `SPI_SETWORKAREA`.

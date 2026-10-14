# LeftStrip

Windows 11 tray utility that reserves a left strip of the primary monitor (Shell AppBar) and hosts one existing top-level window there. Native C++/Win32 — no .NET, Electron, injection, or service.

Default strip width: 20% of the monitor. The target app stays a normal separate window.

## Quick start

**Installer:** run `LeftStrip-Setup-*.exe` ([Releases](https://github.com/yaronf/LeftStrip/releases) or build with `package.cmd`). Installs to `%LocalAppData%\Programs\LeftStrip`; config in `%AppData%\LeftStrip\`.

**Portable:** put `LeftStrip.exe` and `LeftStrip.ini` in a writable folder together.

1. Start the app you want in the strip, then start LeftStrip.
2. Tray icon (or strip) → **Select existing window**.
3. Adjust **Strip width** or edit the ini + **Reload configuration**.
4. Optional: **Start at sign-in**. Exit with **Exit and restore**.

LeftStrip never launches the target. It attaches when exactly one window matches the saved filters.

## Docs

| | |
|---|---|
| [Configuration](docs/configuration.md) | Ini location, keys, filters, startup/`--quit` |
| [Build & test](docs/build.md) | MSVC, CMake, installer, integration test |
| [Limits](docs/limits.md) | What AppBars can and cannot do |
| [VALIDATION.md](VALIDATION.md) | What was checked on a real desktop |

## License

MIT — see [LICENSE.txt](LICENSE.txt).

# Limits

LeftStrip uses a Shell **AppBar**. That shrinks the work area; it is not a second monitor or a hard clip region.

- Maximized windows usually honor the strip; free-floating windows, custom maximize, games, exclusive/borderless fullscreen, and apps that use full monitor bounds can overlap it. Snap zones are unchanged.
- Other taskbars/AppBars on the left edge negotiate space; the “outermost N%” is not guaranteed.
- Targets may refuse resize (min width, `WM_GETMINMAXINFO`, self-repositioning). Electron min-width is an app setting — widen the strip or change the app.
- Elevated/protected targets may ignore a non-elevated LeftStrip (UIPI). Same elevation only if required; LeftStrip does not self-elevate.
- Eligible: visible, unowned top-level windows with titles. Not: dialogs, tool windows, cloaked windows. Some Store/UWP/hosted apps lack a usable window.
- Primary monitor only. DPI/display/AppBar changes renegotiate; mixed-DPI, hot-plug, and virtual desktops need local verification. Session-wide, not per virtual desktop.
- No reparenting or chrome stripping. Optional topmost can cover other windows.
- Other apps’ layouts after remove are up to Windows.
- Unsigned binary: Smart App Control / reputation tooling may block unknown local builds.

## API references

- [Application desktop toolbars](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars)
- [SHAppBarMessage](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shappbarmessage)
- [GetWindowPlacement](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowplacement)
- [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)

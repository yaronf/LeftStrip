#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <dwmapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cwctype>
#include <cstdlib>
#include <cstdio>
#include "resource.h"

// LeftStrip: a separate AppBar owns the reservation. The target stays top-level.
constexpr wchar_t ClassName[] = L"LeftStrip.Native.v1";
/// Private window messages: AppBar callback, tray icon, and a posted relayout.
constexpr UINT Callback = WM_APP + 1, Tray = WM_APP + 2, Relayout = WM_APP + 3;
/// First menu id for one row per window. Fixed commands stay below this.
constexpr UINT PickBase = 1000;
constexpr UINT CmdEdit = 1, CmdReload = 2, CmdKeepTop = 3, CmdStartup = 4, CmdPause = 5, CmdExit = 6, CmdAbout = 7;
/// A width row's command id is this plus the percent.
constexpr UINT WidthCmdBase = 100;
/// Percents offered in the menu. Any value from the minimum to the maximum is still accepted from the ini.
constexpr int WidthChoices[] = {10, 15, 20, 25, 30, 40, 50};

/// Win32 long paths stop at 32,767 characters. One more holds the terminating null.
constexpr DWORD LongPathChars = 32768;
constexpr int TitleChars = 1024;
/// Class names are limited to 256 characters, including the null.
constexpr int ClassChars = 256;
constexpr DWORD SettingChars = 2048;
/// Painted title is cut off so a long caption cannot fill the strip.
constexpr size_t StatusTitleChars = 80;
constexpr size_t MenuChoiceCap = 200;
constexpr size_t MenuLabelChars = 180;
constexpr int LogLineChars = 160;

constexpr int DefaultWidthPercent = 20;
constexpr int MinWidthPercent = 5;
/// Also the cap when a docked window will not shrink to the configured percent.
constexpr int MaxWidthPercent = 50;
/// The shell rejects a zero-width AppBar, and DrawIconEx needs a positive size.
constexpr int MinPx = 1;
constexpr int DefaultPollMs = 1000;
constexpr int MinPollMs = 250;
constexpr int MaxPollMs = 10000;
/// AlwaysOnTop and RestoreOnExit are stored as these two strings.
constexpr wchar_t IniYes[] = L"1";
constexpr wchar_t IniNo[] = L"0";

/// The docked window's left edge must sit within this many pixels of the strip.
constexpr int FitAlignSlackPx = 16;
/// The visible frame must extend past the strip by this many pixels, on two identical polls, before the strip grows.
constexpr int FitHysteresisPx = 12;

constexpr UINT_PTR PollTimerId = 1;
constexpr UINT TrayIconId = 1;

/// Design pixels at 96 DPI. Scale() multiplies them by the strip window's DPI.
constexpr int HamburgerMarginPx = 14;
constexpr int HamburgerSizePx = 28;
constexpr int HamburgerPadDiv = 5;
constexpr int HamburgerBarDiv = 9;
constexpr int HamburgerBarMinPx = 2;
constexpr int HamburgerBars = 3;
constexpr int StripMarginPx = 16;
constexpr int StripIconPx = 48;
constexpr int IconLoadPx = 128;
/// The strip window exists at this size until Layout claims the work area.
constexpr int InitialStripPx = 1;

constexpr COLORREF StripColor = RGB(30, 30, 30);
constexpr COLORREF HamburgerColor = RGB(220, 220, 220);
constexpr COLORREF StatusColor = RGB(210, 210, 210);

/// English (US) Unicode version block. 0x0409 is the language, 1200 (0x04B0) is the code page.
constexpr wchar_t ProductVersionQuery[] = L"\\StringFileInfo\\040904B0\\ProductVersion";
HINSTANCE app{};
HICON iconLarge{}, iconSmall{};
HWND bar{}, target{};
UINT taskbarCreated{};
bool registered{}, positioning{}, quitting{}, paused{}, fullscreen{};
RECT strip{};
WINDOWPLACEMENT original{sizeof(WINDOWPLACEMENT)};
DWORD targetPid{};
HANDLE targetProcess{}; // Retained to distinguish a reused HWND/PID from the original process.
bool originalTopmost{}, originalToolWindow{};
RECT originalScreenNormal{};
std::wstring ini, exe, processFilter, titleFilter, classFilter;
int percent = DefaultWidthPercent, interval = DefaultPollMs;
int fittedWidth = 0, pendingFit = 0; // Wider than percent when the target will not shrink.
bool keepTop = false, restoreOnExit = true;
std::vector<HWND> choices;

/// Case-fold so filter compares ignore case.
std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
/// Window caption.
std::wstring Title(HWND w) { wchar_t s[TitleChars]{}; GetWindowTextW(w, s, TitleChars); return s; }
/// Win32 class name.
std::wstring Class(HWND w) { wchar_t s[ClassChars]{}; GetClassNameW(w, s, ClassChars); return s; }
/// Executable file name.
std::wstring Process(HWND w) {
    DWORD pid{}; GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t path[LongPathChars]{}; DWORD n = LongPathChars;
    bool ok = QueryFullProcessImageNameW(p, 0, path, &n) != FALSE; CloseHandle(p);
    if (!ok) return {};
    std::wstring s(path, n); auto i = s.find_last_of(L"\\/"); return s.substr(i == s.npos ? 0 : i + 1);
}
/// A real top-level window: visible, titled, not owned, not a tool window, not cloaked.
bool Eligible(HWND w) {
    DWORD pid{}; GetWindowThreadProcessId(w, &pid);
    if (w == bar || pid == GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return false;
    if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    DWORD cloaked{}; DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return !cloaked && !Title(w).empty();
}
/// One [LeftStrip] value from the ini.
std::wstring Setting(const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t value[SettingChars]{}; GetPrivateProfileStringW(L"LeftStrip", key, fallback, value, SettingChars, ini.c_str()); return value;
}
/// Write one ini value, or warn if the file is not writable.
void Save(const wchar_t* key, const std::wstring& value) {
    if (!WritePrivateProfileStringW(L"LeftStrip", key, value.c_str(), ini.c_str()))
        MessageBoxW(bar, L"Cannot save LeftStrip.ini. Ensure the config location is writable.", L"LeftStrip", MB_ICONWARNING);
}
/// Seed the keys when LeftStrip.ini.default cannot be copied.
void WriteDefaultIni(const std::wstring& path) {
    WritePrivateProfileStringW(L"LeftStrip", L"WidthPercent", std::to_wstring(DefaultWidthPercent).c_str(), path.c_str());
    WritePrivateProfileStringW(L"LeftStrip", L"ProcessName", L"", path.c_str());
    WritePrivateProfileStringW(L"LeftStrip", L"TitleContains", L"", path.c_str());
    WritePrivateProfileStringW(L"LeftStrip", L"WindowClass", L"", path.c_str());
    WritePrivateProfileStringW(L"LeftStrip", L"PollMilliseconds", std::to_wstring(DefaultPollMs).c_str(), path.c_str());
    WritePrivateProfileStringW(L"LeftStrip", L"AlwaysOnTop", IniYes, path.c_str());
    WritePrivateProfileStringW(L"LeftStrip", L"RestoreOnExit", IniYes, path.c_str());
}
/// Portable: LeftStrip.ini beside the exe. Installed: %AppData%\LeftStrip\LeftStrip.ini
std::wstring ResolveIni(const std::wstring& exePath) {
    std::wstring dir = exePath.substr(0, exePath.find_last_of(L"\\/") + 1);
    std::wstring portable = dir + L"LeftStrip.ini";
    if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) return portable;

    PWSTR appdata{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &appdata)))
        return portable;
    std::wstring folder = std::wstring(appdata) + L"\\LeftStrip";
    CoTaskMemFree(appdata);
    CreateDirectoryW(folder.c_str(), nullptr);
    std::wstring dest = folder + L"\\LeftStrip.ini";
    if (GetFileAttributesW(dest.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring seed = dir + L"LeftStrip.ini.default";
        if (!CopyFileW(seed.c_str(), dest.c_str(), TRUE)) WriteDefaultIni(dest);
    }
    return dest;
}
/// True when the command line contains --quit.
bool HasQuitSwitch() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool quit = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--quit") == 0) { quit = true; break; }
    }
    LocalFree(argv);
    return quit;
}
/// AppBar header bound to the strip window.
APPBARDATA Data() { APPBARDATA d{}; d.cbSize = sizeof(d); d.hWnd = bar; return d; }
/// Give the reserved work area back to the shell.
void Unregister() {
    if (registered) { auto d = Data(); SHAppBarMessage(ABM_REMOVE, &d); registered = false; }
}
/// The HWND still belongs to the process we attached.
bool SameTarget() {
    if (!target || !IsWindow(target) || !targetProcess || WaitForSingleObject(targetProcess, 0) != WAIT_TIMEOUT) return false;
    DWORD pid{}; GetWindowThreadProcessId(target, &pid); return pid == targetPid;
}
/// Non-tool windows store placement in workspace coordinates. sign +1 converts to screen, -1 back.
void ShiftPlacement(RECT& rc, HMONITOR monitor, int sign) {
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(monitor, &mi)) return;
    OffsetRect(&rc, sign * (mi.rcWork.left - mi.rcMonitor.left), sign * (mi.rcWork.top - mi.rcMonitor.top));
}
/// Put the window back where it was, then forget it.
void ReleaseTarget() {
    if (SameTarget() && restoreOnExit) {
        SetWindowPos(target, originalTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        WINDOWPLACEMENT restore = original;
        restore.rcNormalPosition = originalScreenNormal;
        if (!originalToolWindow)
            ShiftPlacement(restore.rcNormalPosition, MonitorFromRect(&originalScreenNormal, MONITOR_DEFAULTTONEAREST), -1);
        SetWindowPlacement(target, &restore);
    }
    target = nullptr; targetPid = 0;
    if (targetProcess) CloseHandle(targetProcess);
    targetProcess = nullptr;
    fittedWidth = 0; pendingFit = 0;
}
/// Append one line to %LocalAppData%\LeftStrip\LeftStrip.log, not beside the ini.
void Log(const wchar_t* line) {
    PWSTR root{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &root))) return;
    std::wstring folder = std::wstring(root) + L"\\LeftStrip";
    CoTaskMemFree(root);
    CreateDirectoryW(folder.c_str(), nullptr);
    std::wstring path = folder + L"\\LeftStrip.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") != 0 || !f) return;
    SYSTEMTIME t{}; GetLocalTime(&t);
    fwprintf(f, L"%02u:%02u:%02u %s\n", t.wHour, t.wMinute, t.wSecond, line);
    fclose(f);
}
/// Dock this window and remember how to restore it.
bool Attach(HWND w) {
    if (w == target && SameTarget()) return true;
    ReleaseTarget();
    DWORD pid{}; GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)};
    if (!p || !GetWindowPlacement(w, &saved)) { if (p) CloseHandle(p); return false; }
    target = w; targetPid = pid; targetProcess = p; original = saved;
    originalToolWindow = (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    originalScreenNormal = saved.rcNormalPosition;
    if (!originalToolWindow)
        ShiftPlacement(originalScreenNormal, MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), 1);
    originalTopmost = (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    return true;
}
/// outer is the Win32 window rect. visible drops the DWM shadow when the attribute is available.
bool WindowFrames(HWND w, RECT& outer, RECT& visible) {
    if (!GetWindowRect(w, &outer)) return false;
    visible = outer;
    RECT extended{};
    if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &extended, sizeof(extended))))
        visible = extended;
    return true;
}
/// Visible bounds. Extended frame bounds exclude the DWM shadow.
bool VisibleFrame(HWND w, RECT& frame) {
    RECT outer{};
    return WindowFrames(w, outer, frame);
}
void Layout();
/// Configured percent, or the latched width when a docked target refuses to shrink (up to MaxWidthPercent).
int StripWidth(const RECT& monitor) {
    int monitorWidth = monitor.right - monitor.left;
    int configured = MulDiv(monitorWidth, percent, 100);
    int cap = MulDiv(monitorWidth, MaxWidthPercent, 100);
    int width = std::max(configured, fittedWidth);
    return std::min(std::max(width, MinPx), cap);
}
/// Grow once the docked window's visible edge stays past the strip. Require two identical
/// readings so a window animation cannot move the work area on every tick.
void ConsiderFit() {
    if (!registered || fullscreen || !SameTarget() || !IsWindowVisible(target) || IsIconic(target) || IsZoomed(target))
        return;
    if (strip.right <= strip.left) return;
    RECT frame{};
    if (!VisibleFrame(target, frame)) return;
    if (std::abs(frame.left - strip.left) > FitAlignSlackPx) { pendingFit = 0; return; }
    int overhang = frame.right - strip.right;
    if (overhang <= FitHysteresisPx) { pendingFit = 0; return; }
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    int cap = MulDiv(mi.rcMonitor.right - mi.rcMonitor.left, MaxWidthPercent, 100);
    int proposal = std::min<int>(frame.right - strip.left, cap);
    if (proposal <= fittedWidth + FitHysteresisPx) { pendingFit = 0; return; }
    if (proposal != pendingFit) {
        wchar_t line[LogLineChars];
        swprintf_s(line, L"fit pending overhang=%d proposal=%d strip=%d", overhang, proposal, strip.right - strip.left);
        Log(line);
        pendingFit = proposal;
        return;
    }
    fittedWidth = proposal;
    pendingFit = 0;
    wchar_t line[LogLineChars];
    swprintf_s(line, L"fit latch width=%d (was strip %d)", fittedWidth, strip.right - strip.left);
    Log(line);
    Layout();
}
/// A full-height strip sits flush with the screen, which makes Windows square the corners.
/// Ask for the normal rounded style so the corner gaps stay transparent.
void PreferRounded(HWND w) {
    if (!w) return;
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(w, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}
/// Size the attached window to the strip, including its invisible frame.
void PlaceTarget() {
    if (paused || fullscreen || !registered || !SameTarget() || !IsWindowVisible(target) || IsIconic(target)) return;
    PreferRounded(target);
    // A maximized target obeys the reduced work area, so restore it before moving.
    if (IsZoomed(target)) { ShowWindowAsync(target, SW_RESTORE); return; }
    RECT wanted = strip, outer{}, visible{};
    if (WindowFrames(target, outer, visible)) {
        wanted.left -= visible.left - outer.left; wanted.top -= visible.top - outer.top;
        wanted.right += outer.right - visible.right; wanted.bottom += outer.bottom - visible.bottom;
    }
    bool topNow = (GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    bool topWanted = keepTop || originalTopmost;
    if (!EqualRect(&outer, &wanted) || topNow != topWanted)
        SetWindowPos(target, topWanted ? HWND_TOPMOST : HWND_NOTOPMOST,
            wanted.left, wanted.top, wanted.right-wanted.left, wanted.bottom-wanted.top,
            SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS | (topNow == topWanted ? SWP_NOZORDER : 0));
}
/// Claim the left edge and move the strip window there.
void Layout() {
    if (!registered || positioning || paused) return;
    positioning = true;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    auto d = Data(); d.uEdge = ABE_LEFT; d.rc = mi.rcMonitor;
    int width = StripWidth(mi.rcMonitor);
    d.rc.right = d.rc.left + width;
    SHAppBarMessage(ABM_QUERYPOS, &d); d.rc.right = d.rc.left + width;
    if (EqualRect(&strip, &d.rc)) { positioning = false; PlaceTarget(); return; }
    SHAppBarMessage(ABM_SETPOS, &d); strip = d.rc;
    SetWindowPos(bar, HWND_BOTTOM, strip.left, strip.top, strip.right-strip.left, strip.bottom-strip.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    PreferRounded(bar);
    positioning = false; PlaceTarget();
}
/// Register the strip as a shell AppBar.
bool Register() {
    if (registered) return true;
    auto d = Data(); d.uCallbackMessage = Callback;
    registered = SHAppBarMessage(ABM_NEW, &d) != 0;
    if (registered) { strip = {}; Layout(); }
    return registered;
}
/// EnumWindows callback: keep windows that match every set filter.
BOOL CALLBACK Find(HWND w, LPARAM param) {
    if (!Eligible(w)) return TRUE;
    auto* found = reinterpret_cast<std::vector<HWND>*>(param);
    if (!processFilter.empty() && Lower(Process(w)) != Lower(processFilter)) return TRUE;
    if (!titleFilter.empty() && Lower(Title(w)).find(Lower(titleFilter)) == std::wstring::npos) return TRUE;
    if (!classFilter.empty() && Lower(Class(w)) != Lower(classFilter)) return TRUE;
    found->push_back(w); return TRUE;
}
std::wstring shownStatus;
/// Copy painted on the strip: waiting, or the attached window.
std::wstring StatusText() {
    if (!SameTarget()) {
        std::wstring message = L"Use the menu to select an app.";
        if (processFilter.empty() && titleFilter.empty() && classFilter.empty()) return message;
        message += L"\n\nOr open ";
        if (!processFilter.empty()) message += processFilter;
        if (!titleFilter.empty()) {
            if (!processFilter.empty()) message += L" — ";
            message += titleFilter;
        } else if (processFilter.empty()) message += classFilter;
        message += L" to place it here.";
        return message;
    }
    std::wstring name = Process(target), title = Title(target);
    if (title.size() > StatusTitleChars) title.resize(StatusTitleChars);
    std::wstring message = name.empty() ? title : name;
    if (!name.empty() && !title.empty()) message += L"\n" + title;
    message += L"\n\nUse the menu to choose a different window.";
    return message;
}
/// Attach a unique match, place it, and repaint only when the status changes.
void Tick() {
    if (paused) return;
    if (!SameTarget()) {
        ReleaseTarget();
        // Never guess if all filters are empty, or more than one window matches.
        if (!processFilter.empty() || !titleFilter.empty() || !classFilter.empty()) {
            std::vector<HWND> matches; EnumWindows(Find, reinterpret_cast<LPARAM>(&matches));
            if (matches.size() == 1) Attach(matches.front());
        }
    }
    PlaceTarget();
    ConsiderFit();
    std::wstring status = StatusText();
    if (status != shownStatus) { shownStatus = status; InvalidateRect(bar, nullptr, FALSE); }
}
/// Apply the ini and restart the poll timer.
void Load() {
    std::wstring width = std::to_wstring(DefaultWidthPercent), poll = std::to_wstring(DefaultPollMs);
    percent = std::clamp(_wtoi(Setting(L"WidthPercent", width.c_str()).c_str()), MinWidthPercent, MaxWidthPercent);
    interval = std::clamp(_wtoi(Setting(L"PollMilliseconds", poll.c_str()).c_str()), MinPollMs, MaxPollMs);
    processFilter = Setting(L"ProcessName"); titleFilter = Setting(L"TitleContains"); classFilter = Setting(L"WindowClass");
    keepTop = Setting(L"AlwaysOnTop", IniYes) == IniYes;
    restoreOnExit = Setting(L"RestoreOnExit", IniYes) != IniNo;
    SetTimer(bar, PollTimerId, interval, nullptr);
}
/// True when the current user's Run key contains LeftStrip.
bool StartupEnabled() {
    HKEY key{}; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key)) return false;
    bool yes = RegQueryValueExW(key, L"LeftStrip", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key); return yes;
}
/// Add or remove that Run value.
void ToggleStartup() {
    bool enabled = StartupEnabled(); HKEY key{};
    LSTATUS err = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (!err) {
        std::wstring command = L"\"" + exe + L"\"";
        err = enabled ? RegDeleteValueW(key, L"LeftStrip") : RegSetValueExW(key, L"LeftStrip", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
        RegCloseKey(key);
    }
    if (err) MessageBoxW(bar, L"Could not change sign-in startup.", L"LeftStrip", MB_ICONERROR);
}
/// Show or remove the notification icon.
void TrayIcon(bool add) {
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = bar; n.uID = TrayIconId;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; n.uCallbackMessage = Tray;
    n.hIcon = iconSmall ? iconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(n.szTip, L"LeftStrip - right-click for controls");
    Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE, &n);
}
/// Design pixels at this window's DPI.
int Scale(HWND w, int px) {
    UINT dpi = GetDpiForWindow(w);
    return MulDiv(px, dpi ? (int)dpi : USER_DEFAULT_SCREEN_DPI, USER_DEFAULT_SCREEN_DPI);
}
/// Menu button in the top-left of the strip.
RECT HamburgerRect(HWND w) {
    int m = Scale(w, HamburgerMarginPx), s = Scale(w, HamburgerSizePx);
    return RECT{m, m, m + s, m + s};
}
/// Three bars inside that button.
void DrawHamburger(HDC dc, const RECT& box) {
    int pad = (box.bottom - box.top) / HamburgerPadDiv;
    int thick = std::max(HamburgerBarMinPx, (int)(box.bottom - box.top) / HamburgerBarDiv);
    int step = (box.bottom - box.top - pad * 2 - thick) / 2;
    HBRUSH brush = CreateSolidBrush(HamburgerColor);
    for (int i = 0; i < HamburgerBars; ++i) {
        RECT line{box.left + pad, box.top + pad + i * step, box.right - pad, box.top + pad + i * step + thick};
        FillRect(dc, &line, brush);
    }
    DeleteObject(brush);
}
/// ProductVersion string from the executable's version resource.
std::wstring ProductVersion() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return {};
    DWORD unused = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &unused);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return {};
    void* value = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), ProductVersionQuery, &value, &len) || !value || !len)
        return {};
    return std::wstring(static_cast<wchar_t*>(value));
}
/// Task dialog callback that opens the project link.
HRESULT CALLBACK AboutLink(HWND, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
/// Version, copyright, and the GitHub link.
void About() {
    std::wstring version = ProductVersion();
    std::wstring heading = version.empty() ? L"LeftStrip" : L"LeftStrip " + version;
    std::wstring body =
        L"Reserves a left strip of the primary monitor and places one existing window there.\n\n"
        L"Copyright (c) 2026 Yaron Sheffer\n"
        L"<A HREF=\"https://github.com/yaronf/LeftStrip\">github.com/yaronf/LeftStrip</A>";
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_HICON_MAIN;
    cfg.hMainIcon = iconLarge ? iconLarge : LoadIconW(nullptr, IDI_APPLICATION);
    cfg.pszWindowTitle = L"About LeftStrip";
    cfg.pszMainInstruction = heading.c_str();
    cfg.pszContent = body.c_str();
    cfg.pfCallback = AboutLink;
    if (FAILED(TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr)))
        MessageBoxW(bar, (heading + L"\n\n" + L"Copyright (c) 2026 Yaron Sheffer").c_str(), L"About LeftStrip", MB_OK);
}
/// Shared menu for the tray icon, the hamburger, and a right-click.
void Menu() {
    HMENU menu = CreatePopupMenu(), pick = CreatePopupMenu(), widths = CreatePopupMenu(); choices.clear();
    EnumWindows([](HWND w, LPARAM)->BOOL { if (Eligible(w) && choices.size()<MenuChoiceCap) choices.push_back(w); return TRUE; }, 0);
    for (size_t i=0; i<choices.size(); ++i) {
        std::wstring label = Process(choices[i]) + L" - " + Title(choices[i]);
        std::replace(label.begin(), label.end(), L'&', L'_');
        AppendMenuW(pick, MF_STRING, PickBase+i, label.substr(0, MenuLabelChars).c_str());
    }
    if (choices.empty()) AppendMenuW(pick, MF_GRAYED, 0, L"No eligible windows");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pick), L"Select existing window");
    for (int n : WidthChoices) AppendMenuW(widths, MF_STRING | (percent==n ? MF_CHECKED:0), WidthCmdBase+n, (std::to_wstring(n)+L"%").c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(widths), L"Strip width");
    AppendMenuW(menu, MF_STRING, CmdEdit, L"Edit configuration");
    AppendMenuW(menu, MF_STRING, CmdReload, L"Reload configuration");
    AppendMenuW(menu, MF_STRING | (keepTop ? MF_CHECKED:0), CmdKeepTop, L"Keep target on top");
    AppendMenuW(menu, MF_STRING | (StartupEnabled() ? MF_CHECKED:0), CmdStartup, L"Start at sign-in");
    AppendMenuW(menu, MF_STRING, CmdPause, paused ? L"Resume reservation" : L"Pause and release space");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdAbout, L"About LeftStrip");
    AppendMenuW(menu, MF_STRING, CmdExit, L"Exit and restore");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(bar);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x,p.y,0,bar,nullptr);
    DestroyMenu(menu); PostMessageW(bar, WM_NULL, 0,0);
    if (cmd >= PickBase && cmd < PickBase+choices.size()) {
        HWND w = choices[cmd-PickBase];
        if (IsWindow(w) && Attach(w)) {
            processFilter = Process(w); titleFilter = Title(w); classFilter = Class(w);
            Save(L"ProcessName", processFilter); Save(L"TitleContains", titleFilter); Save(L"WindowClass", classFilter);
            Tick();
        } else MessageBoxW(bar, L"Cannot access this window. It may have closed or require the same elevation level.", L"LeftStrip", MB_ICONWARNING);
    } else if (cmd >= WidthCmdBase + MinWidthPercent && cmd <= WidthCmdBase + MaxWidthPercent) {
        percent = static_cast<int>(cmd - WidthCmdBase); fittedWidth = 0; pendingFit = 0;
        Save(L"WidthPercent", std::to_wstring(percent)); Layout();
    }
    else switch(cmd) {
        case CmdEdit: ShellExecuteW(bar,L"open",L"notepad.exe", (L"\""+ini+L"\"").c_str(),nullptr,SW_SHOWNORMAL); break;
        case CmdReload: ReleaseTarget(); Load(); Layout(); Tick(); break;
        case CmdKeepTop: keepTop=!keepTop; Save(L"AlwaysOnTop", keepTop ? IniYes : IniNo); Tick(); break;
        case CmdStartup: ToggleStartup(); break;
        case CmdPause: if (!paused) { paused=true; Unregister(); ReleaseTarget(); ShowWindow(bar,SW_HIDE); }
                else { paused=false; Register(); Tick(); } break;
        case CmdExit: SendMessageW(bar,WM_CLOSE,0,0); break;
        case CmdAbout: About(); break;
    }
}
/// Strip window: AppBar callbacks, input, and paint.
LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated && taskbarCreated) { registered=false; TrayIcon(true); if (!paused) Register(); return 0; }
    if (msg == Callback) {
        if (wp == ABN_POSCHANGED && !positioning) PostMessageW(w,Relayout,0,0);
        if (wp == ABN_FULLSCREENAPP) {
            fullscreen = lp != 0;
            SetWindowPos(w,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if (SameTarget() && keepTop && !originalTopmost)
                SetWindowPos(target,fullscreen?HWND_NOTOPMOST:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
        }
        return 0;
    }
    switch(msg) {
        case Tray: if (lp==WM_RBUTTONUP || lp==WM_LBUTTONUP) Menu(); return 0;
        case WM_LBUTTONUP: {
            RECT hit = HamburgerRect(w);
            POINT click{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&hit, click)) Menu();
            return 0;
        }
        case WM_CONTEXTMENU: Menu(); return 0;
        case Relayout: case WM_DISPLAYCHANGE: Layout(); return 0;
        case WM_DPICHANGED: PostMessageW(w,Relayout,0,0); return 0;
        case WM_SETTINGCHANGE: if (!positioning) PostMessageW(w,Relayout,0,0); return 0;
        case WM_TIMER: Tick(); return 0;
        case WM_ACTIVATE: if (registered) { auto d=Data(); SHAppBarMessage(ABM_ACTIVATE,&d); } break;
        case WM_WINDOWPOSCHANGED: if (registered) { auto d=Data(); SHAppBarMessage(ABM_WINDOWPOSCHANGED,&d); } break;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(w,&ps);
            // A width change only invalidates the new edge. Drop that clip and repaint the whole strip,
            // or the previous text wrap stays beside the new one.
            SelectClipRgn(dc, nullptr);
            RECT r{}; GetClientRect(w,&r);
            FillRect(dc, &r, (HBRUSH)GetClassLongPtrW(w, GCLP_HBRBACKGROUND));
            RECT burger = HamburgerRect(w);
            DrawHamburger(dc, burger);
            int margin = Scale(w, StripMarginPx);
            int iconSize = std::min(Scale(w, StripIconPx), std::max(MinPx, (int)r.right - margin * 2));
            int iconX = (r.right - iconSize) / 2;
            int iconY = r.bottom - margin - iconSize;
            if (iconLarge) DrawIconEx(dc, iconX, iconY, iconLarge, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,StatusColor);
            RECT text{margin, burger.bottom + margin, r.right - margin, iconY - margin};
            std::wstring message = StatusText();
            if (text.bottom > text.top)
                DrawTextW(dc, message.c_str(), -1, &text, DT_WORDBREAK | DT_TOP | DT_LEFT);
            EndPaint(w,&ps); return 0;
        }
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wp) SendMessageW(w,WM_CLOSE,0,0); return 0;
        case WM_CLOSE: if (!quitting) { quitting=true; KillTimer(w,PollTimerId); Unregister(); ReleaseTarget(); TrayIcon(false); DestroyWindow(w); } return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
/// One instance. --quit closes the running copy and does not start another.
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (HasQuitSwitch()) { if (HWND w=FindWindowW(ClassName,nullptr)) PostMessageW(w,WM_CLOSE,0,0); return 0; }
    HANDLE mutex = CreateMutexW(nullptr,FALSE,L"Local\\LeftStrip.Native.v1");
    if (!mutex || GetLastError()==ERROR_ALREADY_EXISTS) { if(mutex)CloseHandle(mutex); return 0; }
    app = instance;
    iconLarge = (HICON)LoadImageW(app, MAKEINTRESOURCEW(IDI_MAINICON), IMAGE_ICON, IconLoadPx, IconLoadPx, 0);
    iconSmall = (HICON)LoadImageW(app, MAKEINTRESOURCEW(IDI_MAINICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wchar_t path[LongPathChars]{}; GetModuleFileNameW(nullptr,path,LongPathChars); exe=path;
    ini = ResolveIni(exe);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{sizeof(wc)}; wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=instance; wc.lpszClassName=ClassName;
    wc.hIcon = iconLarge; wc.hIconSm = iconSmall;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(StripColor);
    RegisterClassExW(&wc);
    bar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,ClassName,L"LeftStrip",WS_POPUP,0,0,InitialStripPx,InitialStripPx,nullptr,nullptr,instance,nullptr);
    PreferRounded(bar);
    if (!bar) {
        DeleteObject(wc.hbrBackground);
        if (iconLarge) DestroyIcon(iconLarge);
        if (iconSmall) DestroyIcon(iconSmall);
        CloseHandle(mutex); return 1;
    }
    Load(); Tick(); TrayIcon(true);
    if (!Register()) {
        MessageBoxW(bar,L"Windows could not register the AppBar. LeftStrip will exit.",L"LeftStrip",MB_ICONERROR);
        SendMessageW(bar,WM_CLOSE,0,0);
    } else Tick();
    MSG msg{}; while (GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    Unregister(); ReleaseTarget(); DeleteObject(wc.hbrBackground);
    if (iconLarge) DestroyIcon(iconLarge);
    if (iconSmall) DestroyIcon(iconSmall);
    CloseHandle(mutex); return 0;
}



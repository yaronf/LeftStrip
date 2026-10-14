#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cwctype>
#include <cstdlib>
#include "resource.h"

// LeftStrip: a separate AppBar owns the reservation. The target stays top-level.
constexpr wchar_t ClassName[] = L"LeftStrip.Native.v1";
constexpr UINT Callback = WM_APP + 1, Tray = WM_APP + 2, Relayout = WM_APP + 3;
constexpr UINT PickBase = 1000;
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
int percent = 20, interval = 1000;
bool keepTop = false, restoreOnExit = true;
std::vector<HWND> choices;

std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
std::wstring Title(HWND w) { wchar_t s[1024]{}; GetWindowTextW(w, s, 1024); return s; }
std::wstring Class(HWND w) { wchar_t s[256]{}; GetClassNameW(w, s, 256); return s; }
std::wstring Process(HWND w) {
    DWORD pid{}; GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t path[32768]{}; DWORD n = 32768;
    bool ok = QueryFullProcessImageNameW(p, 0, path, &n) != FALSE; CloseHandle(p);
    if (!ok) return {};
    std::wstring s(path, n); auto i = s.find_last_of(L"\\/"); return s.substr(i == s.npos ? 0 : i + 1);
}
bool Eligible(HWND w) {
    DWORD pid{}; GetWindowThreadProcessId(w, &pid);
    if (w == bar || pid == GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return false;
    if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    DWORD cloaked{}; DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return !cloaked && !Title(w).empty();
}
std::wstring Setting(const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t value[2048]{}; GetPrivateProfileStringW(L"LeftStrip", key, fallback, value, 2048, ini.c_str()); return value;
}
void Save(const wchar_t* key, const std::wstring& value) {
    if (!WritePrivateProfileStringW(L"LeftStrip", key, value.c_str(), ini.c_str()))
        MessageBoxW(bar, L"Cannot save LeftStrip.ini. Move the folder to a writable location.", L"LeftStrip", MB_ICONWARNING);
}
APPBARDATA Data() { APPBARDATA d{}; d.cbSize = sizeof(d); d.hWnd = bar; return d; }
void Unregister() {
    if (registered) { auto d = Data(); SHAppBarMessage(ABM_REMOVE, &d); registered = false; }
}
bool SameTarget() {
    if (!target || !IsWindow(target) || !targetProcess || WaitForSingleObject(targetProcess, 0) != WAIT_TIMEOUT) return false;
    DWORD pid{}; GetWindowThreadProcessId(target, &pid); return pid == targetPid;
}
void ReleaseTarget() {
    if (SameTarget() && restoreOnExit) {
        SetWindowPos(target, originalTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        WINDOWPLACEMENT restore = original;
        restore.rcNormalPosition = originalScreenNormal;
        if (!originalToolWindow) {
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromRect(&originalScreenNormal, MONITOR_DEFAULTTONEAREST), &mi))
                OffsetRect(&restore.rcNormalPosition, mi.rcMonitor.left-mi.rcWork.left, mi.rcMonitor.top-mi.rcWork.top);
        }
        SetWindowPlacement(target, &restore);
    }
    target = nullptr; targetPid = 0;
    if (targetProcess) CloseHandle(targetProcess);
    targetProcess = nullptr;
}
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
    if (!originalToolWindow) {
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi))
            OffsetRect(&originalScreenNormal, mi.rcWork.left-mi.rcMonitor.left, mi.rcWork.top-mi.rcMonitor.top);
    }
    originalTopmost = (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    return true;
}
void PlaceTarget() {
    if (paused || fullscreen || !registered || !SameTarget() || !IsWindowVisible(target) || IsIconic(target)) return;
    // A maximized target obeys the reduced work area, so restore it before moving.
    if (IsZoomed(target)) { ShowWindowAsync(target, SW_RESTORE); return; }
    RECT wanted = strip, outer{}, visible{};
    if (GetWindowRect(target, &outer) && SUCCEEDED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)))) {
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
void Layout() {
    if (!registered || positioning || paused) return;
    positioning = true;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    auto d = Data(); d.uEdge = ABE_LEFT; d.rc = mi.rcMonitor;
    int width = MulDiv(mi.rcMonitor.right-mi.rcMonitor.left, percent, 100);
    d.rc.right = d.rc.left + width;
    SHAppBarMessage(ABM_QUERYPOS, &d); d.rc.right = d.rc.left + width;
    if (EqualRect(&strip, &d.rc)) { positioning = false; PlaceTarget(); return; }
    SHAppBarMessage(ABM_SETPOS, &d); strip = d.rc;
    SetWindowPos(bar, HWND_BOTTOM, strip.left, strip.top, strip.right-strip.left, strip.bottom-strip.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    positioning = false; PlaceTarget();
}
bool Register() {
    if (registered) return true;
    auto d = Data(); d.uCallbackMessage = Callback;
    registered = SHAppBarMessage(ABM_NEW, &d) != 0;
    if (registered) { strip = {}; Layout(); }
    return registered;
}
BOOL CALLBACK Find(HWND w, LPARAM param) {
    if (!Eligible(w)) return TRUE;
    auto* found = reinterpret_cast<std::vector<HWND>*>(param);
    if (!processFilter.empty() && Lower(Process(w)) != Lower(processFilter)) return TRUE;
    if (!titleFilter.empty() && Lower(Title(w)).find(Lower(titleFilter)) == std::wstring::npos) return TRUE;
    if (!classFilter.empty() && Lower(Class(w)) != Lower(classFilter)) return TRUE;
    found->push_back(w); return TRUE;
}
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
    PlaceTarget(); InvalidateRect(bar, nullptr, FALSE);
}
void Load() {
    percent = std::clamp(_wtoi(Setting(L"WidthPercent", L"20").c_str()), 5, 50);
    interval = std::clamp(_wtoi(Setting(L"PollMilliseconds", L"1000").c_str()), 250, 10000);
    processFilter = Setting(L"ProcessName"); titleFilter = Setting(L"TitleContains"); classFilter = Setting(L"WindowClass");
    keepTop = Setting(L"AlwaysOnTop", L"0") == L"1";
    restoreOnExit = Setting(L"RestoreOnExit", L"1") != L"0";
    SetTimer(bar, 1, interval, nullptr);
}
bool StartupEnabled() {
    HKEY key{}; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key)) return false;
    bool yes = RegQueryValueExW(key, L"LeftStrip", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key); return yes;
}
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
void TrayIcon(bool add) {
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = bar; n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; n.uCallbackMessage = Tray;
    n.hIcon = iconSmall ? iconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(n.szTip, L"LeftStrip - right-click for controls");
    Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE, &n);
}
void Menu() {
    HMENU menu = CreatePopupMenu(), pick = CreatePopupMenu(), widths = CreatePopupMenu(); choices.clear();
    EnumWindows([](HWND w, LPARAM)->BOOL { if (Eligible(w) && choices.size()<200) choices.push_back(w); return TRUE; }, 0);
    for (size_t i=0; i<choices.size(); ++i) {
        std::wstring label = Process(choices[i]) + L" - " + Title(choices[i]);
        std::replace(label.begin(), label.end(), L'&', L'_');
        AppendMenuW(pick, MF_STRING, PickBase+i, label.substr(0,180).c_str());
    }
    if (choices.empty()) AppendMenuW(pick, MF_GRAYED, 0, L"No eligible windows");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pick), L"Select existing window");
    for (int n : {10,15,20,25,30,40,50}) AppendMenuW(widths, MF_STRING | (percent==n ? MF_CHECKED:0), 100+n, (std::to_wstring(n)+L"%").c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(widths), L"Strip width");
    AppendMenuW(menu, MF_STRING, 1, L"Edit configuration");
    AppendMenuW(menu, MF_STRING, 2, L"Reload configuration");
    AppendMenuW(menu, MF_STRING | (keepTop ? MF_CHECKED:0), 3, L"Keep target on top");
    AppendMenuW(menu, MF_STRING | (StartupEnabled() ? MF_CHECKED:0), 4, L"Start at sign-in");
    AppendMenuW(menu, MF_STRING, 5, paused ? L"Resume reservation" : L"Pause and release space");
    AppendMenuW(menu, MF_STRING, 6, L"Exit and restore");
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
    } else if (cmd>=105 && cmd<=150) { percent = cmd-100; Save(L"WidthPercent", std::to_wstring(percent)); Layout(); }
    else switch(cmd) {
        case 1: ShellExecuteW(bar,L"open",L"notepad.exe", (L"\""+ini+L"\"").c_str(),nullptr,SW_SHOWNORMAL); break;
        case 2: ReleaseTarget(); Load(); Layout(); Tick(); break;
        case 3: keepTop=!keepTop; Save(L"AlwaysOnTop",keepTop?L"1":L"0"); Tick(); break;
        case 4: ToggleStartup(); break;
        case 5: if (!paused) { paused=true; Unregister(); ReleaseTarget(); ShowWindow(bar,SW_HIDE); }
                else { paused=false; Register(); Tick(); } break;
        case 6: SendMessageW(bar,WM_CLOSE,0,0); break;
    }
}
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
        case WM_CONTEXTMENU: Menu(); return 0;
        case Relayout: case WM_DISPLAYCHANGE: Layout(); return 0;
        case WM_DPICHANGED: PostMessageW(w,Relayout,0,0); return 0;
        case WM_SETTINGCHANGE: if (!positioning) PostMessageW(w,Relayout,0,0); return 0;
        case WM_TIMER: Tick(); return 0;
        case WM_ACTIVATE: if (registered) { auto d=Data(); SHAppBarMessage(ABM_ACTIVATE,&d); } break;
        case WM_WINDOWPOSCHANGED: if (registered) { auto d=Data(); SHAppBarMessage(ABM_WINDOWPOSCHANGED,&d); } break;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(w,&ps); RECT r{}; GetClientRect(w,&r);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(210,210,210)); InflateRect(&r,-16,-16);
            std::wstring text = SameTarget() ? L"LeftStrip\n\nTarget attached.\nRight-click here or the tray icon for controls."
                : L"LeftStrip\n\nWaiting for one matching window.\n\nRight-click here or the tray icon to select an open window.\n\nIf several windows match, refine the configuration.";
            DrawTextW(dc,text.c_str(),-1,&r,DT_WORDBREAK); EndPaint(w,&ps); return 0;
        }
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wp) SendMessageW(w,WM_CLOSE,0,0); return 0;
        case WM_CLOSE: if (!quitting) { quitting=true; KillTimer(w,1); Unregister(); ReleaseTarget(); TrayIcon(false); DestroyWindow(w); } return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // One instance per interactive session; --quit does not start a reservation.
    if (wcscmp(args,L"--quit")==0) { if (HWND w=FindWindowW(ClassName,nullptr)) PostMessageW(w,WM_CLOSE,0,0); return 0; }
    HANDLE mutex = CreateMutexW(nullptr,FALSE,L"Local\\LeftStrip.Native.v1");
    if (!mutex || GetLastError()==ERROR_ALREADY_EXISTS) { if(mutex)CloseHandle(mutex); return 0; }
    app = instance;
    iconLarge = (HICON)LoadImageW(app, MAKEINTRESOURCEW(IDI_MAINICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    iconSmall = (HICON)LoadImageW(app, MAKEINTRESOURCEW(IDI_MAINICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768); exe=path;
    ini=exe.substr(0,exe.find_last_of(L"\\/")+1)+L"LeftStrip.ini";
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc=WndProc; wc.hInstance=instance; wc.lpszClassName=ClassName;
    wc.hIcon = iconLarge; wc.hIconSm = iconSmall;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(RGB(30,30,30));
    RegisterClassExW(&wc);
    bar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,ClassName,L"LeftStrip",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
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



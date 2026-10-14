#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

// Real desktop integration check. Run only in a disposable desktop/session:
// temporarily reserves a strip, creates its own target, and restores both.
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) { return DefWindowProcW(h,m,w,l); }
void Pump(DWORD ms) { DWORD end=GetTickCount()+ms; do { MSG m; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&m);DispatchMessageW(&m);} Sleep(20); } while((LONG)(end-GetTickCount())>0); }
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { puts("Usage: IntegrationTest.exe <absolute-path-to-isolated-LeftStrip.exe>");return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FindWindowW(L"LeftStrip.Native.v1",nullptr)) { puts("Close LeftStrip before testing."); return 2; }
    std::wstring path=argv[1], ini=path.substr(0,path.find_last_of(L"\\/")+1)+L"LeftStrip.ini";
    WritePrivateProfileStringW(L"LeftStrip",L"WindowClass",L"LeftStrip.Integration.Target",ini.c_str());
    WritePrivateProfileStringW(L"LeftStrip",L"ProcessName",L"",ini.c_str());
    WritePrivateProfileStringW(L"LeftStrip",L"TitleContains",L"",ini.c_str());
    RECT before{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&before,0);
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=Proc; wc.lpszClassName=L"LeftStrip.Integration.Target";RegisterClassW(&wc);
    HWND target=CreateWindowW(wc.lpszClassName,L"Integration target",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        before.left+100,before.top+100,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    RECT original{}; GetWindowRect(target,&original);
    std::wstring cmd=L"\""+path+L"\""; STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if(!CreateProcessW(path.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)) { DestroyWindow(target);return 2; }
    Pump(3500);
    RECT reserved{}, positioned{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&reserved,0);
    DwmGetWindowAttribute(target,DWMWA_EXTENDED_FRAME_BOUNDS,&positioned,sizeof(positioned));
    RECT barRect{}; HWND bar=FindWindowW(L"LeftStrip.Native.v1",nullptr); GetWindowRect(bar,&barRect);
    printf("Shell=%p bar=%p barRect=%ld,%ld,%ld,%ld target=%ld,%ld,%ld,%ld\n",GetShellWindow(),bar,barRect.left,barRect.top,barRect.right,barRect.bottom,positioned.left,positioned.top,positioned.right,positioned.bottom);
    bool reserve=reserved.left>before.left && reserved.right==before.right;
    bool moved=abs(positioned.right-reserved.left)<=2 && abs(positioned.top-before.top)<=2;
    HWND app=FindWindowW(L"LeftStrip.Native.v1",nullptr); if(app) PostMessageW(app,WM_CLOSE,0,0);
    Pump(2000);
    RECT after{}, restored{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&after,0);GetWindowRect(target,&restored);
    bool area=EqualRect(&before,&after)!=FALSE, window=EqualRect(&original,&restored)!=FALSE;
    bool exit=WaitForSingleObject(pi.hProcess,2000)==WAIT_OBJECT_0;
    printf("Reservation: %s\nTarget placement: %s\nWork area restored: %s\nTarget restored: %s\nClean exit: %s\n",reserve?"PASS":"FAIL",moved?"PASS":"FAIL",area?"PASS":"FAIL",window?"PASS":"FAIL",exit?"PASS":"FAIL");
    printf("Work area before %ld,%ld,%ld,%ld; reserved %ld,%ld,%ld,%ld\n",before.left,before.top,before.right,before.bottom,reserved.left,reserved.top,reserved.right,reserved.bottom);
    DestroyWindow(target); CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return reserve&&moved&&area&&window&&exit?0:1;
}


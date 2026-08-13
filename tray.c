#include "tray.h"

#define TRAY_CLASS_NAME      L"OverlayTrayHostWndClass"
#define WM_TRAYICON          (WM_APP + 1)
#define ID_TRAY_QUIT         1001
#define ID_TRAY_FAST_MODE    1002
#define TRAY_ICON_ID         1

static HMENU           h_menu     = NULL;
static HWND            h_tray_wnd = NULL;
static NOTIFYICONDATAW nid;
static TrayCallback    callback;

static bool fast_mode = false;

static void show_tray_menu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    h_menu = CreatePopupMenu();
    if (!h_menu) return;
    AppendMenuW(h_menu, MF_STRING | (fast_mode ? MF_CHECKED : MF_UNCHECKED), 
            ID_TRAY_FAST_MODE, L"Fast Mode");
    AppendMenuW(h_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h_menu, MF_STRING, ID_TRAY_QUIT, L"Quit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(h_menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(h_menu);
}

static LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_TRAYICON:
            switch (lp)
            {
                case WM_RBUTTONUP:
                    show_tray_menu(hwnd);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_COMMAND:
            if (LOWORD(wp) == ID_TRAY_QUIT)
            {
                if (callback.on_quit) callback.on_quit();
                return 0;
            }
            if (LOWORD(wp) == ID_TRAY_FAST_MODE)
            {
                fast_mode = !fast_mode;
                CheckMenuItem(h_menu, ID_TRAY_FAST_MODE, 
                        MF_BYCOMMAND | (fast_mode ? MF_CHECKED : MF_UNCHECKED));
                if (callback.on_fast_mode) callback.on_fast_mode();
                return 0;
            }

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool tray_init(HINSTANCE h_instance, LPCWSTR tool_tip)
{
    memset(&callback, 0, sizeof(callback));
    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = tray_wnd_proc;
    wc.hInstance     = h_instance;
    wc.lpszClassName = TRAY_CLASS_NAME;
    if (!RegisterClassW(&wc)) return false;

    h_tray_wnd = CreateWindowExW(
        0, TRAY_CLASS_NAME, L"", 0,
        0, 0, 0, 0,
        HWND_MESSAGE, NULL, h_instance, NULL);
    if (!h_tray_wnd) return false;

    memset(&nid, 0, sizeof(nid));
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = h_tray_wnd;
    nid.uID              = TRAY_ICON_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);

    if (tool_tip) lstrcpynW(nid.szTip, tool_tip, ARRAYSIZE(nid.szTip));
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;

    return true;
}

void tray_set_callback(const TrayCallback *c)
{
    if (c) callback = *c;
}

void tray_shutdown(void)
{
    if (h_tray_wnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(h_tray_wnd);
        h_tray_wnd = NULL;
    }
    UnregisterClassW(TRAY_CLASS_NAME, GetModuleHandleW(NULL));
}

#include "overlay.h"
#include <stdlib.h>

#define OVERLAY_CLASS_NAME L"OverlayEditHostWndClass"
#define TIMER_ID_CTRL_POLL 1
#define CTRL_POLL_MS       15

#define OVERLAY_WIDTH      640
#define OVERLAY_HEIGHT     40

#define OVERLAY_COLOR      RGB(28, 28, 30)
#define EDIT_COLOR         RGB(45, 45, 48)
#define EDIT_TEXT_COLOR    RGB(230, 230, 230)

#define DISGUISE_MAGIC     0x5A5A5A5A

static HWND    h_overlay       = NULL;
static HWND    h_edit          = NULL;
static WNDPROC orig_edit_proc  = NULL;
static HFONT   h_font          = NULL;
static HBRUSH  h_bg_brush      = NULL;
static HBRUSH  h_edit_brush    = NULL;
static bool    ctrl_hold_block = false;
static char   *pending_text    = NULL;
static HHOOK   h_kbd_hook      = NULL;
static bool    win_down        = false;
static bool    win_enter_down  = false;

static OverlayCallback callback;


static bool overlay_is_emtpy(void)
{
    return GetWindowTextLengthW(h_edit) == 0;
}

static void overlay_clear_text(void)
{
    SetWindowTextW(h_edit, L"");
}

static void overlay_show(void)
{
    HWND h_fg = GetForegroundWindow();
    DWORD fg_thread = h_fg ? GetWindowThreadProcessId(h_fg, NULL) : 0;
    DWORD this_thread = GetCurrentThreadId();
    bool attached = false;

    if (fg_thread && fg_thread != this_thread)
        attached = AttachThreadInput(this_thread, fg_thread, true);

    ShowWindow(h_overlay, SW_SHOWNA);
    SetForegroundWindow(h_overlay);
    SetFocus(h_edit);

    if (attached)
        AttachThreadInput(this_thread, fg_thread, false);
}

static void overlay_hide(void)
{
    ShowWindow(h_overlay, SW_HIDE);
}

static char *overlay_get_text_utf8(void)
{
    int wlen = GetWindowTextLengthW(h_edit);
    wchar_t *wbuf = malloc((size_t)(wlen+1) * sizeof(wchar_t));
    if (!wbuf) return NULL;
    GetWindowTextW(h_edit, wbuf, wlen+1);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    char *u8 = malloc((size_t)u8len > 0 ? (size_t)u8len : 1);
    if (u8) WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, u8, u8len, NULL, NULL);
    free(wbuf);
    return u8;
}

void static send_disguise_key(void)
{
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[0].ki.dwExtraInfo = DISGUISE_MAGIC;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CONTROL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].ki.dwExtraInfo = DISGUISE_MAGIC;

    SendInput(2, inputs, sizeof(INPUT));
}

static LRESULT CALLBACK kdb_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION)
    {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lp;
        bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        bool up   = wp == WM_KEYUP   || wp == WM_SYSKEYUP;

        if (kb->dwExtraInfo == DISGUISE_MAGIC)
        {
            return CallNextHookEx(NULL, code, wp, lp);
        }

        if (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN)
        {
            if (down)    win_down = true;
            else if (up) win_down = false;

            return CallNextHookEx(NULL, code, wp, lp);
        }

        if (kb->vkCode == VK_RETURN)
        {
            if (down && win_down)
            {
                if (!win_enter_down)
                {
                    win_enter_down = true;
                    if (!ctrl_hold_block) overlay_show();
                }
                send_disguise_key();
                return 1;
            }

            if (up && win_enter_down)
            {
                win_enter_down = false;
                return 1;
            }
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN)
    {
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

        switch (wp)
        {
            case VK_ESCAPE:
                overlay_clear_text();
                overlay_hide();
                return 0;

            case VK_TAB:
                overlay_hide();
                return 0;

            case VK_RETURN:
                if (overlay_is_emtpy())
                {
                    overlay_hide();
                    return 0;
                }

                char *text = overlay_get_text_utf8();

                if (ctrl)
                {
                    overlay_clear_text();
                    overlay_hide();
                    pending_text = text;
                    ctrl_hold_block = true;
                    SetTimer(h_overlay, TIMER_ID_CTRL_POLL, CTRL_POLL_MS, NULL);
                    return 0;
                }
                else if (shift)
                {
                    overlay_clear_text();
                    if (callback.on_submit)
                        callback.on_submit(text, OA_SEND_CONTINUE, callback.arg);
                }
                else
                {
                    overlay_clear_text();
                    overlay_hide();
                    if (callback.on_submit)
                        callback.on_submit(text, OA_SEND, callback.arg);
                }

                free(text);
                return 0;

            default:
                break;
        }
    }

    if (msg == WM_CHAR)
    {
        if (wp == VK_TAB || wp == VK_RETURN || wp == VK_ESCAPE)
            return 0;
    }
    return CallWindowProcW(orig_edit_proc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK overlay_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_CTLCOLOREDIT:
            {
                HDC hdc_edit = (HDC)wp;
                SetTextColor(hdc_edit, EDIT_TEXT_COLOR);
                SetBkColor(hdc_edit, EDIT_COLOR);
                SetBkMode(hdc_edit, OPAQUE);
                return (LRESULT)h_edit_brush;
            }

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && IsWindowVisible(hwnd))
                overlay_hide();
            return 0;

        case WM_TIMER:
            if (wp == TIMER_ID_CTRL_POLL)
            {
                if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000))
                {
                    KillTimer(hwnd, TIMER_ID_CTRL_POLL);
                    ctrl_hold_block = false;
                    if (pending_text)
                    {
                        if (callback.on_submit)
                            callback.on_submit(pending_text, OA_SEND_HOLD, callback.arg);

                        free(pending_text);
                        pending_text = NULL;
                    }
                }
            }
            return 0;

        case WM_SIZE:
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (h_edit) MoveWindow(h_edit, 4, 4, rc.right-8, rc.bottom-8, true);
                return 0;
            }

        case WM_DESTROY:
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool overlay_init(HINSTANCE h_instance)
{
    memset(&callback, 0, sizeof(callback));
    h_bg_brush   = CreateSolidBrush(OVERLAY_COLOR);
    h_edit_brush = CreateSolidBrush(EDIT_COLOR);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = overlay_wnd_proc;
    wc.hInstance     = h_instance;
    wc.lpszClassName = OVERLAY_CLASS_NAME;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = h_bg_brush;
    if (!RegisterClassW(&wc)) return false;

    h_kbd_hook = SetWindowsHookExW(WH_KEYBOARD_LL, kdb_proc, h_instance, 0);
    if (!h_kbd_hook) return false;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - OVERLAY_WIDTH) / 2;
    int y = screen_h * 0.75;

    h_overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        OVERLAY_CLASS_NAME, L"",
        WS_POPUP | WS_BORDER,
        x, y, OVERLAY_WIDTH, OVERLAY_HEIGHT,
        NULL, NULL, h_instance, NULL);
    if (!h_overlay) return false;

    SetLayeredWindowAttributes(h_overlay, 0, 235, LWA_ALPHA);

    h_edit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, OVERLAY_WIDTH, OVERLAY_HEIGHT,
        h_overlay, NULL, h_instance, NULL);
    if (!h_edit) return false;

    h_font = CreateFontW(
        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (h_font) SendMessageW(h_edit, WM_SETFONT, (WPARAM)h_font, TRUE);

    orig_edit_proc = (WNDPROC)SetWindowLongPtrW(h_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);

    return true;
}

void overlay_set_callback(const OverlayCallback *c)
{
    if (c) callback = *c;
}

void overlay_shutdown(void)
{
    if (h_overlay)
    {
        KillTimer(h_overlay, TIMER_ID_CTRL_POLL);
    }
    if (pending_text)
    {
        free(pending_text);
        pending_text = NULL;
    }
    if (h_edit && orig_edit_proc)
    {
        SetWindowLongPtrW(h_edit, GWLP_WNDPROC, (LONG_PTR)orig_edit_proc);
    }
    if (h_font)
    {
        DeleteObject(h_font);
        h_font = NULL;
    }
    if (h_overlay)
    {
        DestroyWindow(h_overlay);
        h_overlay = NULL;
    }
    UnregisterClassW(OVERLAY_CLASS_NAME, GetModuleHandleW(NULL));
    if (h_bg_brush)
    {
        DeleteObject(h_bg_brush);
        h_bg_brush = NULL;
    }
    if (h_edit_brush)
    {
        DeleteObject(h_edit_brush);
        h_edit_brush = NULL;
    }
}

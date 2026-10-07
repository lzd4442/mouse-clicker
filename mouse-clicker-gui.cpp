/*
 * Mouse Auto-Clicker - Win32 GUI v2
 * Compile: x86_64-w64-mingw32-g++ -o mouse-clicker-gui.exe mouse-clicker-gui.cpp -lkernel32 -luser32 -lgdi32 -lcomctl32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── IDs ──────────────────────────────────────────────
#define ID_START_BTN     101
#define ID_STOP_BTN      102
#define ID_INTERVAL_EDIT 103
#define ID_SPEED_SLIDER  104
#define ID_LEFT_RAD      105
#define ID_RIGHT_RAD     106
#define ID_MID_RAD       107
#define ID_COUNT_STATIC  108
#define ID_STATUS_STATIC 109
#define ID_REC_BTN       110
#define ID_REC_COUNT     111
#define ID_CPS_STATIC    112

// ── Globals ─────────────────────────────────────────
static HWND   g_hwnd          = NULL;
static HWND   g_count_label   = NULL;
static HWND   g_status_label  = NULL;
static HWND   g_rec_count_lbl = NULL;
static HWND   g_cps_label     = NULL;
static HWND   g_start_btn     = NULL;
static HWND   g_stop_btn      = NULL;
static HWND   g_rec_btn       = NULL;

static volatile BOOL g_running    = FALSE;
static volatile BOOL g_paused     = FALSE;
static volatile int  g_interval   = 100;  // ms
static volatile int  g_click_count = 0;
static volatile int  g_button     = 0;     // 0=L 1=R 2=M
static volatile int  g_cps       = 0;

static volatile BOOL g_recording  = FALSE;
static volatile int  g_rec_count  = 0;
static POINT         g_rec_pts[2000];
static volatile int  g_rec_idx   = 0;

static HANDLE g_thread = NULL;
static HANDLE g_timer_thread = NULL;
static volatile BOOL g_timer_running = FALSE;

// ── Click ────────────────────────────────────────────
static void DoClick(void) {
    DWORD down, up;
    if (g_button == 0)      { down = MOUSEEVENTF_LEFTDOWN;   up = MOUSEEVENTF_LEFTUP; }
    else if (g_button == 1) { down = MOUSEEVENTF_RIGHTDOWN;  up = MOUSEEVENTF_RIGHTUP; }
    else                    { down = MOUSEEVENTF_MIDDLEDOWN; up = MOUSEEVENTF_MIDDLEUP; }

    // Primary: SendInput (preferred, works in most contexts)
    INPUT inp[2];
    memset(inp, 0, sizeof(inp));
    inp[0].type = INPUT_MOUSE;
    inp[0].mi.dwFlags = down;
    inp[1].type = INPUT_MOUSE;
    inp[1].mi.dwFlags = up;

    UINT r = SendInput(2, inp, (int)sizeof(INPUT));
    if (r != 2) {
        // Fallback: mouse_event (works in admin/high-privilege contexts)
        mouse_event(down, 0, 0, 0, 0);
        mouse_event(up, 0, 0, 0, 0);
    }
}

// ── Clicker thread ──────────────────────────────────
static DWORD WINAPI ClickerThread(LPVOID) {
    while (g_running) {
        if (!g_paused) {
            if (g_recording && g_rec_count > 0) {
                POINT pt = g_rec_pts[g_rec_idx % g_rec_count];
                SetCursorPos(pt.x, pt.y);
                Sleep(25);
            }
            DoClick();
            InterlockedIncrement((LONG*)&g_click_count);
        }
        Sleep(g_interval);
    }
    return 0;
}

// ── Timer: update CPS every second ─────────────────
static DWORD WINAPI TimerThread(LPVOID) {
    while (g_timer_running) {
        Sleep(1000);
        int c = InterlockedExchange((LONG*)&g_click_count, g_click_count);
        g_cps = c;
        InterlockedExchange((LONG*)&g_click_count, 0);

        if (g_hwnd && g_cps_label) {
            char buf[32];
        wsprintfA(buf, "%d cps", g_cps);
            SetWindowTextA(g_cps_label, buf);
        }
        if (g_hwnd && g_count_label) {
            char buf[32];
            wsprintfA(buf, "%d", c);
            SetWindowTextA(g_count_label, buf);
        }
    }
    return 0;
}

// ── Helpers ─────────────────────────────────────────
static void UpdateStatus(const wchar_t* txt, COLORREF col) {
    if (!g_hwnd || !g_status_label) return;
    SetWindowTextW(g_status_label, txt);
    InvalidateRect(g_status_label, NULL, TRUE);
}

static void StartClicker(void) {
    if (g_running) return;

    // Read interval
    char buf[32];
    GetWindowTextA(GetDlgItem(g_hwnd, ID_INTERVAL_EDIT), buf, 32);
    int v = atoi(buf);
    if (v < 10) v = 10;
    if (v > 10000) v = 10000;
    g_interval = v;

    g_running = TRUE;
    g_paused = FALSE;
    g_click_count = 0;

    SetWindowTextA(g_start_btn, "Running...");
    SetWindowTextA(g_stop_btn, "■ STOP");
    UpdateStatus(L"⚡ Running...", RGB(0, 200, 83));

    DWORD tid;
    g_thread = CreateThread(NULL, 0, ClickerThread, NULL, 0, &tid);
    g_timer_running = TRUE;
    g_timer_thread = CreateThread(NULL, 0, TimerThread, NULL, 0, &tid);
}

static void StopClicker(void) {
    if (!g_running) return;
    g_running = FALSE;
    g_timer_running = FALSE;

    if (g_thread) { WaitForSingleObject(g_thread, 2000); CloseHandle(g_thread); g_thread = NULL; }
    if (g_timer_thread) { WaitForSingleObject(g_timer_thread, 2000); CloseHandle(g_timer_thread); g_timer_thread = NULL; }

    SetWindowTextA(g_start_btn, "▶ START");
    SetWindowTextA(g_stop_btn, "Stop");
    UpdateStatus(L"⏹ Stopped", RGB(130, 130, 140));
}

static void TogglePause(void) {
    if (!g_running) return;
    g_paused = !g_paused;
    if (g_paused)
        UpdateStatus(L"⏸ Paused", RGB(255, 185, 0));
    else
        UpdateStatus(L"⚡ Running...", RGB(0, 200, 83));
}

// ── Window Proc ─────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFontTitle, hFontBig, hFontNorm, hFontMono;
    static HBRUSH hBgBrush, hGreenBrush, hRedBrush, hYellowBrush, hInputBg;
    static COLORREF TXT_WHITE = RGB(240, 240, 245);
    static COLORREF TXT_GREEN = RGB(0, 220, 80);
    static COLORREF TXT_GRAY  = RGB(130, 130, 140);

    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;

        // ── Brushes ──
        hBgBrush   = CreateSolidBrush(RGB(18, 18, 28));
        hGreenBrush= CreateSolidBrush(RGB(0, 190, 70));
        hRedBrush  = CreateSolidBrush(RGB(210, 45, 55));
        hYellowBrush = CreateSolidBrush(RGB(220, 165, 0));
        hInputBg   = CreateSolidBrush(RGB(28, 28, 44));

        // ── Fonts ──
        hFontTitle = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hFontBig = CreateFontW(52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hFontNorm = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hFontMono = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

        // ── Title bar icon space ──
        CreateWindowW(L"STATIC", L"🖱 Mouse Auto-Clicker",
            WS_CHILD | SS_LEFT, 16, 14, 380, 32, hwnd, NULL, NULL, NULL);
        SendMessage(GetDlgItem(hwnd, 1), WM_SETFONT, (WPARAM)hFontTitle, TRUE);

        // ── Click counter ──
        CreateWindowW(L"STATIC", L"CLICKS",
            WS_CHILD | SS_CENTER, 16, 58, 380, 18, hwnd, NULL, NULL, NULL);
        g_count_label = CreateWindowW(L"STATIC", L"0",
            WS_CHILD | SS_CENTER, 16, 76, 380, 64, hwnd, (HMENU)ID_COUNT_STATIC, NULL, NULL);
        g_cps_label = CreateWindowW(L"STATIC", L"0 cps",
            WS_CHILD | SS_CENTER, 16, 142, 380, 18, hwnd, (HMENU)ID_CPS_STATIC, NULL, NULL);

        // ── Status bar ──
        g_status_label = CreateWindowW(L"STATIC", L"⏹ Ready — press Start or F6",
            WS_CHILD | SS_CENTER, 16, 165, 380, 20, hwnd, (HMENU)ID_STATUS_STATIC, NULL, NULL);

        // ── Interval section ──
        CreateWindowW(L"STATIC", L"⏱  Interval (ms)",
            WS_CHILD, 16, 200, 140, 18, hwnd, NULL, NULL, NULL);

        HWND hEdit = CreateWindowW(L"EDIT", L"100",
            WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER,
            160, 197, 70, 24, hwnd, (HMENU)ID_INTERVAL_EDIT, NULL, NULL);
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMono, TRUE);

        // Speed slider
        HWND hSlider = CreateWindowW(TRACKBAR_CLASSW, NULL,
            WS_CHILD | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            16, 224, 380, 28, hwnd, (HMENU)ID_SPEED_SLIDER, NULL, NULL);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 2000));
        SendMessage(hSlider, TBM_SETPOS, TRUE, 100);
        SendMessage(hSlider, TBM_SETPAGESIZE, 0, 50);

        // ── Button group ──
        CreateWindowW(L"STATIC", L"🔘 Mouse Button",
            WS_CHILD, 16, 262, 130, 18, hwnd, NULL, NULL, NULL);

        HWND hLeft = CreateWindowW(L"BUTTON", L"🖱 Left",
            WS_CHILD | WS_GROUP | BS_AUTORADIOBUTTON | WS_TABSTOP,
            150, 260, 80, 22, hwnd, (HMENU)ID_LEFT_RAD, NULL, NULL);
        HWND hRight = CreateWindowW(L"BUTTON", L"🖱 Right",
            WS_CHILD | BS_AUTORADIOBUTTON | WS_TABSTOP,
            235, 260, 80, 22, hwnd, (HMENU)ID_RIGHT_RAD, NULL, NULL);
        HWND hMid = CreateWindowW(L"BUTTON", L"⚙ Middle",
            WS_CHILD | BS_AUTORADIOBUTTON | WS_TABSTOP,
            320, 260, 80, 22, hwnd, (HMENU)ID_MID_RAD, NULL, NULL);
        SendMessage(hLeft, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(hLeft, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(hRight, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(hMid, WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Action buttons ──
        g_start_btn = CreateWindowW(L"BUTTON", L"▶ START",
            WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            16, 296, 185, 48, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
        g_stop_btn = CreateWindowW(L"BUTTON", L"■ STOP",
            WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            211, 296, 185, 48, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
        SendMessage(g_start_btn, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(g_stop_btn, WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Record button ──
        g_rec_btn = CreateWindowW(L"BUTTON", L"📍 Record Positions",
            WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
            16, 354, 185, 30, hwnd, (HMENU)ID_REC_BTN, NULL, NULL);
        g_rec_count_lbl = CreateWindowW(L"STATIC", L"0 positions",
            WS_CHILD | SS_LEFT, 210, 358, 185, 22, hwnd, (HMENU)ID_REC_COUNT, NULL, NULL);
        SendMessage(g_rec_btn, WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Hotkey hint ──
        CreateWindowW(L"STATIC",
            L"F6 Start/Stop  |  F7 Pause  |  ESC Exit  |  Click Record → then click screen positions",
            WS_CHILD | SS_CENTER, 16, 392, 380, 18, hwnd, NULL, NULL, NULL);

        // ── Apply fonts ──
        SendMessage(GetDlgItem(hwnd, 1), WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        SendMessage(GetDlgItem(hwnd, 2), WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(g_count_label, WM_SETFONT, (WPARAM)hFontBig, TRUE);
        SendMessage(g_cps_label, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(g_status_label, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(GetDlgItem(hwnd, 8), WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(GetDlgItem(hwnd, 11), WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Init common controls ──
        InitCommonControls();
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int ev = HIWORD(wParam);

        if (id == ID_START_BTN && ev == BN_CLICKED) {
            if (!g_running) StartClicker();
        }
        else if (id == ID_STOP_BTN && ev == BN_CLICKED) {
            StopClicker();
        }
        else if (id == ID_LEFT_RAD   && ev == BN_CLICKED) g_button = 0;
        else if (id == ID_RIGHT_RAD  && ev == BN_CLICKED) g_button = 1;
        else if (id == ID_MID_RAD    && ev == BN_CLICKED) g_button = 2;
        else if (id == ID_INTERVAL_EDIT && ev == EN_CHANGE) {
            char b[32];
            GetWindowTextA(GetDlgItem(hwnd, ID_INTERVAL_EDIT), b, 32);
            int v = atoi(b);
            if (v >= 10 && v <= 2000) {
                HWND hS = GetDlgItem(hwnd, ID_SPEED_SLIDER);
                if (hS) SendMessage(hS, TBM_SETPOS, TRUE, v);
            }
        }
        else if (id == ID_REC_BTN && ev == BN_CLICKED) {
            g_recording = !g_recording;
            g_rec_count = 0;
            g_rec_idx = 0;
            SetWindowTextA(g_rec_btn, g_recording ? "📍 Recording... (ESC to stop)" : "📍 Record Positions");
            if (g_recording) {
                MessageBoxW(hwnd,
                    L"Recording mode ON!\n\nMove your mouse to desired positions and press SPACE to record each point.\nPress ESC to finish.",
                    L"Record", MB_OK);
            }
        }
        break;
    }

    case WM_HSCROLL: {
        HWND hS = (HWND)lParam;
        if (hS && GetWindowLongPtr(hS, GWLP_ID) == ID_SPEED_SLIDER) {
            int pos = SendMessage(hS, TBM_GETPOS, 0, 0);
            char buf[16];
            wsprintfA(buf, "%d", pos);
            SetWindowTextA(GetDlgItem(hwnd, ID_INTERVAL_EDIT), buf);
        }
        break;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_SPACE && g_recording && g_rec_count < 2000) {
            POINT pt;
            GetCursorPos(&pt);
            g_rec_pts[g_rec_count++] = pt;
            char buf[32];
            wsprintfA(buf, "%d positions", g_rec_count);
            SetWindowTextA(g_rec_count_lbl, buf);
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);

        if (hCtrl == g_count_label) {
            SetTextColor(hdc, g_running ? RGB(0, 210, 80) : RGB(220, 220, 230));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (hCtrl == g_status_label) {
            if (g_running && !g_paused) SetTextColor(hdc, RGB(0, 200, 70));
            else if (g_paused) SetTextColor(hdc, RGB(255, 180, 0));
            else SetTextColor(hdc, RGB(130, 130, 145));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        SetTextColor(hdc, RGB(200, 200, 210));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(25, 25, 40));
        SetTextColor(hdc, RGB(0, 215, 80));
        return (LRESULT)hInputBg;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        int bid = GetWindowLongPtr(hCtrl, GWLP_ID);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        if (bid == ID_START_BTN) {
            return (LRESULT)(g_running ? hYellowBrush : hGreenBrush);
        }
        if (bid == ID_STOP_BTN) {
            return (LRESULT)(g_running ? hRedBrush : hRedBrush);
        }
        return (LRESULT)hBgBrush;
    }

    case WM_DESTROY:
        StopClicker();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ── Global hotkey thread ────────────────────────────
static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        // Only react to key-down
        if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && !(lParam & (1 << 31))) {
            if (p->vkCode == VK_F6) {
                if (!g_running) StartClicker();
                else StopClicker();
                return 1;
            }
            if (p->vkCode == VK_F7) {
                TogglePause();
                return 1;
            }
            if (p->vkCode == VK_ESCAPE) {
                if (g_recording) {
                    g_recording = FALSE;
                    SetWindowTextA(g_rec_btn, "📍 Record Positions");
                } else {
                    StopClicker();
                    PostMessage(g_hwnd, WM_QUIT, 0, 0);
                }
                return 1;
            }
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

// ── WinMain ─────────────────────────────────────────
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    // Register
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = h;
    wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 28));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"MouseClicker_v2";
    if (!RegisterClassExW(&wc)) return 1;

    // Create window
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Mouse Auto-Clicker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 428, 475,
        NULL, NULL, h, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    // Low-level keyboard hook for global F6/F7
    HHOOK hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(hHook);
    return 0;
}

/*
 * Mouse Auto-Clicker - Win32 GUI Version
 * Compile: x86_64-w64-mingw32-g++ -o mouse-clicker.exe mouse-clicker-gui.cpp -lwinapi-msvc
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── IDs ──────────────────────────────────────────────
#define ID_START_BTN    101
#define ID_INTERVAL_EDIT 102
#define ID_INTERVAL_UP  103
#define ID_LEFT_RAD    104
#define ID_RIGHT_RAD   105
#define ID_MID_RAD     106
#define ID_COUNT_STATIC 107
#define ID_STATUS_STATIC 108
#define ID_HOTKEY_STATIC 109
#define ID_TITLE_ICO   110
#define ID_TIMER       111
#define ID_PROGRESS    112
#define ID_RECORD_BTN  113

// ── Globals ─────────────────────────────────────────
HWND g_hwnd = NULL;
HWND g_count_label = NULL;
HWND g_status_label = NULL;
HWND g_interval_edit = NULL;
BOOL g_running = FALSE;
BOOL g_paused = FALSE;
int  g_interval_ms = 100;
int  g_click_count = 0;
int  g_button = 0;   // 0=left 1=right 2=middle
int  g_record_mode = FALSE;
POINT g_record_points[1000];
int   g_record_count = 0;
int   g_record_index = 0;
HANDLE g_clicker_thread = NULL;
CRITICAL_SECTION g_cs;

// Click event flags
DWORD g_click_down_flags[3] = {
    MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN
};
DWORD g_click_up_flags[3] = {
    MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP
};

// ── Helpers ──────────────────────────────────────────
void SetStatus(HWND hwnd, const wchar_t* text, COLORREF color) {
    SetWindowTextW(g_status_label, text);
    InvalidateRect(g_status_label, NULL, TRUE);
}

void UpdateCount() {
    wchar_t buf[64];
    swprintf(buf, L"%d", g_click_count);
    SetWindowTextW(g_count_label, buf);
}

void DoClick() {
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = g_click_down_flags[g_button];
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = g_click_up_flags[g_button];
    SendInput(2, inputs, sizeof(INPUT));
}

void DoClickAt(POINT pt) {
    SetCursorPos(pt.x, pt.y);
    Sleep(30);
    DoClick();
}

// ── Clicker Thread ──────────────────────────────────
DWORD WINAPI ClickerThread(LPVOID) {
    while (g_running) {
        if (!g_paused) {
            if (g_record_mode && g_record_count > 0) {
                POINT pt = g_record_points[g_record_index % g_record_count];
                DoClickAt(pt);
                g_record_index++;
            } else {
                DoClick();
            }
            InterlockedIncrement((LONG*)&g_click_count);
            UpdateCount();
        }
        Sleep(g_interval_ms);
    }
    return 0;
}

// ── Window Proc ─────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFontTitle, hFontBig, hFontNorm, hFontMono;
    static HBRUSH hBgBrush, hGreenBrush, hRedBrush, hBlueBrush;
    static HPEN hGreenPen, hRedPen;

    switch (msg) {
    case WM_CREATE: {
        // ── Fonts ──
        hFontTitle = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hFontBig = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hFontNorm = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hFontMono = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

        // ── Colors ──
        hBgBrush = CreateSolidBrush(RGB(15, 15, 25));
        hGreenBrush = CreateSolidBrush(RGB(0, 200, 83));
        hRedBrush = CreateSolidBrush(RGB(220, 53, 69));
        hBlueBrush = CreateSolidBrush(RGB(0, 120, 212));
        hGreenPen = CreatePen(PS_SOLID, 2, RGB(0, 200, 83));
        hRedPen = CreatePen(PS_SOLID, 2, RGB(220, 53, 69));

        // ── Title ──
        CreateWindowW(L"STATIC",
            L"🖱 Mouse Auto-Clicker",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 18, 360, 38, hwnd, NULL, NULL, NULL);
        SendMessage(GetDlgItem(hwnd, 1), WM_SETFONT, (WPARAM)hFontTitle, TRUE);

        // ── Click Counter ──
        CreateWindowW(L"STATIC", L"CLICKS",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 70, 360, 22, hwnd, NULL, NULL, NULL);
        SendMessage(GetDlgItem(hwnd, 2), WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        g_count_label = CreateWindowW(L"STATIC", L"0",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 90, 360, 60, hwnd, (HMENU)ID_COUNT_STATIC, NULL, NULL);
        SendMessage(g_count_label, WM_SETFONT, (WPARAM)hFontBig, TRUE);

        // ── Status ──
        g_status_label = CreateWindowW(L"STATIC", L"⏹ Ready",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 158, 360, 24, hwnd, (HMENU)ID_STATUS_STATIC, NULL, NULL);
        SendMessage(g_status_label, WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Interval ──
        CreateWindowW(L"STATIC", L"点击间隔 (ms)",
            WS_CHILD | WS_VISIBLE,
            20, 196, 130, 20, hwnd, NULL, NULL, NULL);

        g_interval_edit = CreateWindowW(L"EDIT", L"100",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_NUMBER,
            155, 193, 80, 26, hwnd, (HMENU)ID_INTERVAL_EDIT, NULL, NULL);
        SendMessage(g_interval_edit, WM_SETFONT, (WPARAM)hFontMono, TRUE);

        CreateWindowW(L"STATIC", L"ms",
            WS_CHILD | WS_VISIBLE,
            240, 196, 30, 20, hwnd, NULL, NULL, NULL);
        SendMessage(GetDlgItem(hwnd, 7), WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Speed Slider ──
        HWND hSlider = CreateWindowW(L"TRACKBAR_ACK",
            NULL, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            20, 222, 360, 24, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 2000));
        SendMessage(hSlider, TBM_SETPOS, TRUE, 100);
        SendMessage(hSlider, TBM_SETPAGESIZE, 0, 50);

        // ── Mouse Button ──
        CreateWindowW(L"STATIC", L"点击按键",
            WS_CHILD | WS_VISIBLE,
            20, 258, 80, 20, hwnd, NULL, NULL, NULL);

        HWND hLeft = CreateWindowW(L"BUTTON", L"🖱 左键",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON | BS_NOTIFY,
            100, 256, 90, 24, hwnd, (HMENU)ID_LEFT_RAD, NULL, NULL);
        HWND hRight = CreateWindowW(L"BUTTON", L"🖱 右键",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            195, 256, 90, 24, hwnd, (HMENU)ID_RIGHT_RAD, NULL, NULL);
        HWND hMid = CreateWindowW(L"BUTTON", L"⚙ 中键",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            290, 256, 90, 24, hwnd, (HMENU)ID_MID_RAD, NULL, NULL);
        SendMessage(hLeft, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(hRight, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(hMid, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(hLeft, BM_SETCHECK, BST_CHECKED, 0);

        // ── Start/Stop Button ──
        HWND hStart = CreateWindowW(L"BUTTON", L"▶  START",
            WS_CHILD | WS_VISIBLE | BS_CENTER | BS_MULTILINE,
            20, 295, 175, 52, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
        SendMessage(hStart, WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        // ── Record/Position Mode ──
        HWND hRec = CreateWindowW(L"BUTTON", L"📍 录制坐标",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            205, 295, 175, 26, hwnd, (HMENU)ID_RECORD_BTN, NULL, NULL);
        SendMessage(hRec, WM_SETFONT, (WPARAM)hFontNorm, TRUE);

        CreateWindowW(L"STATIC",
            L"F6 开/关  |  F7 暂停  |  ESC 退出",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 360, 360, 20, hwnd, NULL, NULL, NULL);

        // ── Hotkey info ──
        HWND hHotkey = CreateWindowW(L"STATIC",
            L"💡 提示：F6/F7 热键全局生效，无需聚焦窗口",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 385, 360, 20, hwnd, (HMENU)ID_HOTKEY_STATIC, NULL, NULL);

        // Set font colors for labels
        for (int id = 1; id <= 2; id++)
            SendMessage(GetDlgItem(hwnd, id), WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(GetDlgItem(hwnd, 7), WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(GetDlgItem(hwnd, 8), WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        SendMessage(hHotkey, WM_SETFONT, (WPARAM)hFontNorm, TRUE);
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == ID_START_BTN && wmEvent == BN_CLICKED) {
            if (!g_running) {
                // Get interval
                wchar_t buf[32];
                GetWindowTextW(g_interval_edit, buf, 32);
                int interval = _wtoi(buf);
                if (interval < 10) interval = 10;
                if (interval > 10000) interval = 10000;
                g_interval_ms = interval;

                g_running = TRUE;
                g_paused = FALSE;
                g_click_count = 0;
                UpdateCount();

                // Change button
                SetWindowTextW(GetDlgItem(hwnd, ID_START_BTN), L"⏹  STOP");
                SetStatus(hwnd, L"⚡ Running...", RGB(0, 200, 83));

                // Start thread
                DWORD tid;
                g_clicker_thread = CreateThread(NULL, 0, ClickerThread, NULL, 0, &tid);
            } else {
                g_running = FALSE;
                SetWindowTextW(GetDlgItem(hwnd, ID_START_BTN), L"▶  START");
                SetStatus(hwnd, L"⏹ Stopped", RGB(150, 150, 150));
            }
        }
        else if (wmId == ID_LEFT_RAD && wmEvent == BN_CLICKED) g_button = 0;
        else if (wmId == ID_RIGHT_RAD && wmEvent == BN_CLICKED) g_button = 1;
        else if (wmId == ID_MID_RAD && wmEvent == BN_CLICKED) g_button = 2;
        else if (wmId == ID_INTERVAL_EDIT && wmEvent == EN_CHANGE) {
            wchar_t buf[32];
            GetWindowTextW(g_interval_edit, buf, 32);
            int v = _wtoi(buf);
            HWND hSlider = GetDlgItem(hwnd, ID_PROGRESS);
            if (hSlider && v >= 10 && v <= 2000)
                SendMessage(hSlider, TBM_SETPOS, TRUE, v);
        }
        else if (wmId == ID_RECORD_BTN && wmEvent == BN_CLICKED) {
            g_record_mode = !g_record_mode;
            SetWindowTextW(GetDlgItem(hwnd, ID_RECORD_BTN),
                g_record_mode ? L"📍 录制中... (右键保存)" : L"📍 录制坐标");
        }
        break;
    }

    case WM_HSCROLL: {
        HWND hSlider = (HWND)lParam;
        if (hSlider && GetWindowLongPtr(hSlider, GWLP_ID) == ID_PROGRESS) {
            int pos = SendMessage(hSlider, TBM_GETPOS, 0, 0);
            wchar_t buf[16];
            swprintf(buf, L"%d", pos);
            SetWindowTextW(g_interval_edit, buf);
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        if (hCtrl == g_status_label) {
            if (g_running && !g_paused) SetTextColor(hdc, RGB(0, 200, 83));
            else if (g_paused) SetTextColor(hdc, RGB(255, 180, 0));
            else SetTextColor(hdc, RGB(150, 150, 150));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (hCtrl == g_count_label) {
            if (g_running) SetTextColor(hdc, RGB(0, 200, 83));
            else SetTextColor(hdc, RGB(220, 220, 220));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(30, 30, 50));
        SetTextColor(hdc, RGB(0, 200, 83));
        return (LRESULT)CreateSolidBrush(RGB(30, 30, 50));
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (GetWindowLongPtr(hCtrl, GWLP_ID) == ID_START_BTN) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            return (LRESULT)(g_running ? hRedBrush : hGreenBrush);
        }
        return (LRESULT)hBgBrush;
    }

    case WM_DESTROY:
        g_running = FALSE;
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ── Global Keyboard Hook ─────────────────────────────
HHOOK g_hKeyboardHook = NULL;

LRESULT CALLBACK KeyboardHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        DWORD vk = (DWORD)wParam;
        if (vk == VK_F6) {
            PostMessage(g_hwnd, WM_COMMAND, ID_START_BTN, 0);
        } else if (vk == VK_F7) {
            if (g_running) {
                g_paused = !g_paused;
                SetStatus(g_hwnd,
                    g_paused ? L"⏸ Paused" : L"⚡ Running...",
                    g_paused ? RGB(255, 180, 0) : RGB(0, 200, 83));
            }
        } else if (vk == VK_ESCAPE) {
            g_running = FALSE;
            PostMessage(g_hwnd, WM_QUIT, 0, 0);
        }
    }
    return CallNextHookEx(g_hKeyboardHook, code, wParam, lParam);
}

// ── WinMain ─────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nCmdShow) {
    // Register window class
    const wchar_t CLSNAME[] = L"MouseClickerGUI";

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = CreateSolidBrush(RGB(15, 15, 25));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLSNAME;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class.", L"Error", MB_ICONERROR);
        return 1;
    }

    // Create window
    HWND hwnd = CreateWindowExW(
        0, CLSNAME, L"Mouse Auto-Clicker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 480,
        NULL, NULL, hInst, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Failed to create window.", L"Error", MB_ICONERROR);
        return 1;
    }

    g_hwnd = hwnd;
    InitializeCriticalSection(&g_cs);

    // Set window icon
    HICON hIcon = LoadIcon(hInst, IDI_APPLICATION);
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    // Install keyboard hook for global hotkeys
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD, KeyboardHook, NULL, GetCurrentThreadId());

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hKeyboardHook) UnhookWindowsHookEx(g_hKeyboardHook);
    DeleteCriticalSection(&g_cs);

    return 0;
}

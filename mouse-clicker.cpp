// Windows Mouse Auto-Clicker
// Compile: x86_64-w64-mingw32-g++ -o mouse-clicker.exe mouse-clicker.cpp -lkernel32 -luser32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <ctype.h>

// Global state
volatile BOOL g_running = FALSE;
volatile BOOL g_pause = FALSE;
HANDLE g_hotkey_thread = NULL;
HANDLE g_timer_queue = NULL;

// Hotkey thread - listens for F6 to toggle, F7 to pause
DWORD WINAPI HotkeyMonitor(LPVOID lpParam) {
    int ch;
    while (1) {
        ch = _getch();
        if (ch == 0x00 || ch == 0xE0) {
            ch = _getch(); // Extended key
            if (ch == 0x41) { // F6 = toggle
                g_running = !g_running;
                if (g_running) {
                    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    printf("\r[ON ] Auto-clicker %s!       \n", g_pause ? "PAUSED" : "RUNNING");
                } else {
                    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
                    printf("\r[OFF] Auto-clicker stopped. \n");
                }
                SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                printf("  > ");
                fflush(stdout);
            } else if (ch == 0x3F) { // F7 = pause
                g_pause = !g_pause;
                SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                printf("\r[%s] Auto-clicker %s!       \n", g_pause ? "PAUSE" : "RESUME", g_running ? "RUNNING" : "STOPPED");
                SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                printf("  > ");
                fflush(stdout);
            }
        }
    }
    return 0;
}

// Simulate mouse click
void DoClick(DWORD flags) {
    mouse_event(flags, 0, 0, 0, 0);                 // down
    Sleep(10);
    mouse_event(flags ^ MOUSEEVENTF_LEFTUP ^ MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0); // up
}

// Get current click count as string
char* click_count_str(int* count) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d", *count);
    return buf;
}

int main() {
    int interval_ms = 100;
    int button = 0; // 0=left, 1=right, 2=middle
    int count = 0;
    DWORD click_flags[3] = {
        MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP,
        MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP,
        MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP
    };
    const char* button_names[] = {"Left", "Right", "Middle"};
    const char* button_keys[] = {"L", "R", "M"};

    // Console setup
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitle("Mouse Auto-Clicker");
    
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);

    // Banner
    SetConsoleTextAttribute(hOut, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    printf("\n");
    printf("  ╔═══════════════════════════════════════╗\n");
    printf("  ║       Mouse Auto-Clicker v1.0         ║\n");
    printf("  ║   Cross-compiled for Windows (x64)    ║\n");
    printf("  ╚═══════════════════════════════════════╝\n");
    printf("\n");

    // Settings
    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_BLUE);
    printf("  [Settings]\n");
    
    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    printf("  Interval (ms) [current=%d]: ", interval_ms);
    char buf[64];
    if (fgets(buf, sizeof(buf), stdin) && atoi(buf) > 0) {
        interval_ms = atoi(buf);
    }
    
    printf("  Button: L=Left, R=Right, M=Middle [current=%s]: ", button_names[button]);
    if (fgets(buf, sizeof(buf), stdin)) {
        char c = toupper(buf[0]);
        if (c == 'R') button = 1;
        else if (c == 'M') button = 2;
        else button = 0;
    }

    printf("\n");
    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_BLUE);
    printf("  [Hotkeys]\n");
    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    printf("  F6  - Toggle ON/OFF\n");
    printf("  F7  - Pause / Resume\n");
    printf("  ESC - Exit\n");
    printf("\n");

    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    printf("  Status: ");
    SetConsoleTextAttribute(hOut, FOREGROUND_RED);
    printf("STOPPED\n");
    printf("\n");

    // Start hotkey monitor thread
    g_hotkey_thread = CreateThread(NULL, 0, HotkeyMonitor, NULL, 0, NULL);

    // Main click loop
    int console_width = 50;
    COORD coord;
    coord.X = 0;
    coord.Y = 0;
    
    while (1) {
        // Check for ESC
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 27) break; // ESC
        }

        if (g_running && !g_pause) {
            DoClick(click_flags[button]);
            count++;
            
            // Progress display
            SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_BLUE);
            coord.Y = 12;
            SetConsoleCursorPosition(hOut, coord);
            printf("  Clicks: %-10d  Interval: %dms  Button: %s   \n",
                   count, interval_ms, button_names[button]);
            printf("  Status: ");
            SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            printf("RUNNING  ");
            SetConsoleTextAttribute(hOut, FOREGROUND_GREEN);
            printf("[F6=Stop]  [F7=Pause]  [ESC=Exit]          \n");
            
            // Simple progress bar
            int bar_width = 40;
            int filled = (count / 10) % bar_width;
            printf("  [");
            SetConsoleTextAttribute(hOut, FOREGROUND_GREEN);
            for (int i = 0; i < bar_width; i++) {
                printf("%c", i < filled ? '=' : '-');
            }
            SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            printf("] %ds   ", count * interval_ms / 1000);
        }

        if (g_running && !g_pause) {
            Sleep(interval_ms);
        } else {
            Sleep(50);
        }
    }

    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
    printf("\n  Total clicks: %d\n", count);
    printf("  Exiting...\n\n");

    CloseHandle(g_hotkey_thread);
    return 0;
}

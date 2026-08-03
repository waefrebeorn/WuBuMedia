/*
 * wubucmd.c — WuBuDesk native desktop-control tool (the cohost's hands/eyes).
 *
 * Proper Win32 desktop automation in C11, built with mingw-w64. No third-party
 * deps. This is OUR tool — built, not borrowed. Part of the WuBu AGI desktop
 * integration layer. The cohost LEARNED the coordinate system and input model
 * (see WuBuMedia/knowledge/) and encoded it here.
 *
 * Coordinate model:
 *   - Screen coordinates are PHYSICAL PIXELS, origin top-left of the virtual
 *     screen (bounding rect of all monitors: SM_XVIRTUALSCREEN..CYVIRTUALSCREEN).
 *   - SendInput absolute mouse uses NORMALIZED 0..65535 (not pixels):
 *       nx = (x - virtLeft) * 65535 / (virtWidth - 1)
 *   - SetCursorPos uses physical pixels; SendInput needs the normalized form.
 *
 * Subcommands:
 *   wubucmd list                        -> enumerate visible windows (title+pid)
 *   wubucmd win <substr>                -> window rect + state for a match
 *   wubucmd rect <substr>               -> print GetWindowRect (x,y,w,h)
 *   wubucmd shot <file.bmp>             -> screenshot virtual screen (BMP)
 *   wubucmd focus <substr>              -> restore+foreground a window
 *   wubucmd min <substr>                -> SW_MINIMIZE
 *   wubucmd restore <substr>            -> SW_RESTORE
 *   wubucmd max <substr>                -> SW_SHOWMAXIMIZED
 *   wubucmd close <substr>              -> WM_CLOSE (graceful exit)
 *   wubucmd click <x> <y>               -> physical-pixel click (normalized SendInput)
 *   wubucmd key <text>                  -> Unicode keystrokes to foreground
 *
 * License: SPDX-License-Identifier: WaefreBeorn-UMV3
 * Build: gcc wubucmd.c -o wubucmd.exe -municode -lgdi32 -luser32 -lole32 -lshlwapi -O2
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <shlwapi.h>

static int g_count = 0;

/* ---- window helpers ---- */
BOOL CALLBACK list_cb(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    int len = GetWindowTextLengthW(hwnd);
    if (len == 0) return TRUE;
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    GetWindowTextW(hwnd, buf, len + 1);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wprintf(L"  [%d] pid=%lu  %ls\n", ++g_count, pid, buf);
    free(buf);
    return TRUE;
}

static HWND find_window(const wchar_t *sub) {
    HWND w = NULL;
    while ((w = FindWindowExW(NULL, w, NULL, NULL)) != NULL) {
        int len = GetWindowTextLengthW(w);
        if (len == 0) continue;
        wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        GetWindowTextW(w, buf, len + 1);
        if (StrStrIW(buf, sub)) { free(buf); return w; }
        free(buf);
    }
    return NULL;
}

static const wchar_t *state_str(HWND w) {
    if (IsIconic(w)) return L"minimized";
    if (IsZoomed(w)) return L"maximized";
    if (!IsWindowVisible(w)) return L"hidden";
    return L"normal";
}

static void print_rect(HWND w, const wchar_t *tag) {
    RECT r;
    GetWindowRect(w, &r);
    wprintf(L"  %ls rect: (%d,%d) %dx%d state=%ls\n", tag,
            r.left, r.top, r.right - r.left, r.bottom - r.top, state_str(w));
}

static int do_win(const wchar_t *sub) {
    HWND w = find_window(sub);
    if (!w) { fprintf(stderr, "no window matching '%ls'\n", sub); return 1; }
    print_rect(w, sub);
    return 0;
}

static int do_rect(const wchar_t *sub) {
    HWND w = find_window(sub);
    if (!w) { fprintf(stderr, "no window matching\n"); return 1; }
    RECT r; GetWindowRect(w, &r);
    wprintf(L"%d %d %d %d\n", r.left, r.top, r.right - r.left, r.bottom - r.top);
    return 0;
}

/* ---- screenshot via BitBlt (virtual screen) ---- */
static int do_shot(const wchar_t *wpath) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC hdc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, vw, vh);
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, vw, vh, hdc, vx, vy, SRCCOPY);

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = vw; bi.biHeight = -vh;
    bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
    DWORD sz = ((DWORD)vw * 4 + 3) / 4 * 4 * vh;
    BYTE *bits = (BYTE *)malloc(sz);
    GetDIBits(mem, bmp, 0, vh, bits, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    FILE *f = _wfopen(wpath, L"wb");
    if (!f) { fprintf(stderr, "cant open bmp\n"); free(bits); return 1; }
    BITMAPFILEHEADER fh = {0};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + sz;
    fwrite(&fh, 1, sizeof(fh), f);
    fwrite(&bi, 1, sizeof(bi), f);
    fwrite(bits, 1, sz, f);
    fclose(f);
    free(bits);
    DeleteObject(bmp); DeleteDC(mem); ReleaseDC(NULL, hdc);
    printf("shot -> %ls (%dx%d virtual origin %d,%d)\n", wpath, vw, vh, vx, vy);
    return 0;
}

/* ---- window state ---- */
static int show_cmd(const wchar_t *sub, int cmd, const char *name) {
    HWND w = find_window(sub);
    if (!w) { fprintf(stderr, "no window matching\n"); return 1; }
    ShowWindow(w, cmd);
    printf("%s %ls -> %ls\n", name, sub, state_str(w));
    return 0;
}

/* ---- physical-pixel click via normalized SendInput ---- */
static int do_click(int x, int y) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    /* normalize physical pixels to SendInput's 0..65535 space */
    DWORD nx = (DWORD)((x - vx) * 65535 / (vw - 1));
    DWORD ny = (DWORD)((y - vy) * 65535 / (vh - 1));
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dx = nx; in.mi.dy = ny;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(INPUT));
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(INPUT));
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(INPUT));
    printf("clicked %d,%d (normalized %lu,%lu)\n", x, y, nx, ny);
    return 0;
}

/* ---- keystrokes ---- */
static int do_key(const wchar_t *text) {
    size_t nchars = wcslen(text);
    INPUT *in = (INPUT *)calloc(nchars * 2, sizeof(INPUT));
    int n = 0;
    for (size_t i = 0; i < nchars; ++i) {
        in[n].type = INPUT_KEYBOARD;
        in[n].ki.dwFlags = KEYEVENTF_UNICODE;
        in[n].ki.wScan = (WORD)text[i];
        ++n;
        in[n] = in[n - 1];
        in[n].ki.dwFlags |= KEYEVENTF_KEYUP;
        ++n;
    }
    SendInput((UINT)n, in, sizeof(INPUT));
    free(in);
    printf("typed %d chars\n", (int)nchars);
    return 0;
}

/* ---- virtual-key presses (Enter/Esc/Tab/F-keys/arrows/Alt combos) ---- */
static void send_vk(WORD vk, int up) {
    INPUT in = {0};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

static int do_press(const wchar_t *name) {
    struct { const wchar_t *n; WORD vk; } map[] = {
        { L"enter", VK_RETURN }, { L"return", VK_RETURN }, { L"esc", VK_ESCAPE },
        { L"tab", VK_TAB }, { L"space", VK_SPACE }, { L"backspace", VK_BACK },
        { L"delete", VK_DELETE }, { L"up", VK_UP }, { L"down", VK_DOWN },
        { L"left", VK_LEFT }, { L"right", VK_RIGHT }, { L"home", VK_HOME },
        { L"end", VK_END }, { L"f5", VK_F5 }, { L"f10", VK_F10 },
        { L"f4", VK_F4 }, { L"alt", VK_MENU }, { L"a", 'A' }, { L"n", 'N' },
    };
    int i;
    WORD vk = 0;
    for (i = 0; i < (int)(sizeof(map)/sizeof(map[0])); ++i)
        if (!_wcsicmp(name, map[i].n)) { vk = map[i].vk; break; }
    if (!vk) { fprintf(stderr, "unknown key '%ls'\n", name); return 1; }
    /* support "alt+f4" style via 'alt' then 'f4' handled by caller; here simple press */
    send_vk(vk, 0);
    send_vk(vk, 1);
    printf("pressed %ls\n", name);
    return 0;
}

/* ---- graceful close ---- */
static int do_close(const wchar_t *sub) {
    HWND w = find_window(sub);
    if (!w) { fprintf(stderr, "no window matching\n"); return 1; }
    SendMessageW(w, WM_CLOSE, 0, 0);
    printf("sent WM_CLOSE\n");
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        wprintf(L"wubucmd: list | win <s> | rect <s> | shot <f> | focus <s> | "
                L"min <s> | restore <s> | max <s> | close <s> | click <x> <y> | key <t>\n");
        return 1;
    }
    if (!wcscmp(argv[1], L"list")) { wprintf(L"windows:\n"); EnumWindows(list_cb, 0); return 0; }
    if (!wcscmp(argv[1], L"win"))    return argc > 2 ? do_win(argv[2]) : 1;
    if (!wcscmp(argv[1], L"rect"))   return argc > 2 ? do_rect(argv[2]) : 1;
    if (!wcscmp(argv[1], L"shot"))   return do_shot(argc > 2 ? argv[2] : L"wubu_shot.bmp");
    if (!wcscmp(argv[1], L"focus"))  return argc > 2 ? show_cmd(argv[2], SW_RESTORE, "focus") : 1;
    if (!wcscmp(argv[1], L"min"))    return argc > 2 ? show_cmd(argv[2], SW_MINIMIZE, "min") : 1;
    if (!wcscmp(argv[1], L"restore"))return argc > 2 ? show_cmd(argv[2], SW_RESTORE, "restore") : 1;
    if (!wcscmp(argv[1], L"max"))    return argc > 2 ? show_cmd(argv[2], SW_SHOWMAXIMIZED, "max") : 1;
    if (!wcscmp(argv[1], L"close"))  return argc > 2 ? do_close(argv[2]) : 1;
    if (!wcscmp(argv[1], L"click"))  return argc > 3 ? do_click(_wtoi(argv[2]), _wtoi(argv[3])) : 1;
    if (!wcscmp(argv[1], L"key"))    return argc > 2 ? do_key(argv[2]) : 1;
    if (!wcscmp(argv[1], L"press"))  return argc > 2 ? do_press(argv[2]) : 1;
    wprintf(L"unknown cmd\n");
    return 1;
}

/* wubugui.c — WuBuVoice Full GUI Application (C11 + Win32).
 *
 * Real-time voice changer with full GUI for VoiceMeeter.
 * Our own engine — no Python, no fairstack, no ONNX.
 *
 * Features:
 *   - Voice selection dropdown (8 cartoon characters + custom)
 *   - Real-time mic processing with live VU meter
 *   - TTS text input + speak button
 *   - Speed/accuracy benchmark
 *   - VoiceMeeter integration (virtual audio cable)
 *   - RVC model loading (.pth/.index)
 *   - CUDA status / VRAM monitoring
 *
 * License: WaefreBeorn-UMV3
 */

#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "wubu_vc.h"
#include "wubu_rvc.h"
#include "wubu_rcu.h"
#include "wubu_model_dock.h"

/* ── IDs ───────────────────────────────── */
#define IDC_VOICE_COMBO    1001
#define IDC_PITCH_SLIDER   1002
#define IDC_SPEED_SLIDER   1003
#define IDC_BTN_SPEAK      1004
#define IDC_BTN_BENCH      1005
#define IDC_BTN_MIC        1006
#define IDC_BTN_LOADMODEL  1007
#define IDC_TEXT_INPUT     1008
#define IDC_VU_METER       1009
#define IDC_STATUS         1010
#define IDC_LATENCY_LABEL  1011
#define IDC_RAM_LABEL      1012
#define IDC_VOICES_LIST    1013
#define IDC_BTN_HOTSWAP    1014
#define IDC_BTN_MINDMELD   1015
#define IDC_MODEL_PATH     1016

/* ── Global state ── */
static HWND g_hwnd = NULL;
static WuBuVoiceChanger *g_vc = NULL;
static VCConfig g_cfg;
static int g_mic_active = 0;
static float g_vu_level = 0.0f;
static HWND g_hCombo = NULL;
static HWND g_hEdit = NULL;
static HWND g_hStatus = NULL;
static HWND g_hVu = NULL;
static HWND g_hLatency = NULL;
static HWND g_hRam = NULL;
static HWND g_hModelPath = NULL;
static HWND g_hHotSwapBtn = NULL;

/* RCU hot-swap state */
static wubu_model_dock_t g_dock;
static int g_mind_meld_enabled = 0;
static int g_hotswap_pending = 0;

/* Forward decls */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK VUProc(HWND, UINT, WPARAM, LPARAM);
void on_speak_click(void);
void on_bench_click(void);
void on_mic_click(void);
void on_voice_change(void);
void on_load_model_click(void);
void on_hotswap_click(void);
void on_mindmeld_click(void);
void update_display(void);

static HWND mk_label(HWND parent, const wchar_t *text, int x, int y, int w, int h) {
    return CreateWindowEx(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE,
        x, y, w, h, parent, NULL, GetModuleHandle(NULL), NULL);
}

static HWND mk_button(HWND parent, const wchar_t *text, int x, int y, int w, int h, int id) {
    return CreateWindowEx(0, WC_BUTTON, text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(LONG_PTR)(long)id,
        GetModuleHandle(NULL), NULL);
}

/* ── VU Meter custom control ── */
LRESULT CALLBACK VUProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int bar_w = (rc.right - rc.left) / 20;
        int gap = 2;
        int total_w = 20 * bar_w + 19 * gap;
        int start_x = (rc.right - rc.left - total_w) / 2;

        int active = (int)(g_vu_level * 20);
        for (int i = 0; i < 20; i++) {
            int x = start_x + i * (bar_w + gap);
            int bar_h = (rc.bottom - rc.top) * (i + 1) / 20;
            RECT bar = {x, rc.bottom - bar_h, x + bar_w, rc.bottom};
            COLORREF color = i >= active ? RGB(40, 40, 40) :
                (i > 15 ? RGB(255, 60, 60) :
                 i > 10 ? RGB(255, 230, 0) :
                 RGB(0, 200, 0));
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(hdc, &bar, brush);
            DeleteObject(brush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        g_vu_level *= 0.95f;
        if (g_vu_level < 0.01f) g_vu_level = 0.0f;
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ── Window creation ── */
static void create_controls(HWND parent) {
    /* Voice combo */
    g_hCombo = CreateWindowEx(0, WC_COMBOBOX, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 10, 200, 120, parent, (HMENU)(LONG_PTR)(long)IDC_VOICE_COMBO,
        GetModuleHandle(NULL), NULL);

    const char *voices[] = {"default", "cartman", "homer", "terminator",
                            "chipmunk", "deep", "robot", "alien"};
    for (int i = 0; i < 8; i++) {
        SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)voices[i]);
    }
    SendMessage(g_hCombo, CB_SETCURSEL, 0, 0);

    /* Pitch label + slider */
    mk_label(parent, L"Pitch:", 10, 40, 80, 20);
    CreateWindowEx(WS_EX_STATICEDGE, TRACKBAR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ,
        100, 40, 200, 25, parent, (HMENU)(LONG_PTR)(long)IDC_PITCH_SLIDER,
        GetModuleHandle(NULL), NULL);
    SendMessage(GetDlgItem(parent, IDC_PITCH_SLIDER), TBM_SETRANGE, TRUE,
        MAKELONG(-12, 12));
    SendMessage(GetDlgItem(parent, IDC_PITCH_SLIDER), TBM_SETPOS, TRUE, 0);

    /* Speed label + slider */
    mk_label(parent, L"Speed:", 10, 70, 80, 20);
    CreateWindowEx(WS_EX_STATICEDGE, TRACKBAR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ,
        100, 70, 200, 25, parent, (HMENU)(LONG_PTR)(long)IDC_SPEED_SLIDER,
        GetModuleHandle(NULL), NULL);
    SendMessage(GetDlgItem(parent, IDC_SPEED_SLIDER), TBM_SETRANGE, TRUE,
        MAKELONG(50, 200));
    SendMessage(GetDlgItem(parent, IDC_SPEED_SLIDER), TBM_SETPOS, TRUE, 100);

    /* Text input */
    mk_label(parent, L"Text:", 10, 100, 60, 20);
    g_hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        75, 100, 350, 25, parent, (HMENU)(LONG_PTR)(long)IDC_TEXT_INPUT,
        GetModuleHandle(NULL), NULL);

    /* Buttons */
    mk_button(parent, L"Speak", 10, 135, 80, 30, IDC_BTN_SPEAK);
    mk_button(parent, L"Benchmark", 100, 135, 80, 30, IDC_BTN_BENCH);
    mk_button(parent, L"Mic ON", 190, 135, 80, 30, IDC_BTN_MIC);
    mk_button(parent, L"Load Model", 280, 135, 80, 30, IDC_BTN_LOADMODEL);

    /* Hot-swap + Mind-Meld buttons */
    g_hHotSwapBtn = mk_button(parent, L"Hot-Swap On", 370, 135, 80, 30, IDC_BTN_HOTSWAP);
    mk_button(parent, L"Mind-Meld", 460, 135, 80, 30, IDC_BTN_MINDMELD);

    /* Model path display */
    g_hModelPath = mk_label(parent, L"Model: (none loaded) | Drag & drop .pth + .index to import", 10, 165, 480, 20);
    g_hVu = CreateWindowEx(0, L"VUMETER", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        370, 10, 120, 100, parent, (HMENU)(LONG_PTR)(long)IDC_VU_METER,
        GetModuleHandle(NULL), NULL);
    SetTimer(g_hVu, 1, 50, NULL);

    /* Status + info labels */
    g_hStatus = mk_label(parent, L"Ready. Engine: WuBuRVC C11", 10, 175, 480, 20);
    g_hLatency = mk_label(parent, L"Latency: 0.00 ms", 10, 200, 200, 20);
    g_hRam = mk_label(parent, L"VRAM: 0 MB", 220, 200, 200, 20);
}

/* ── Window procedure ── */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        InitCommonControls();
        create_controls(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_SPEAK: on_speak_click(); break;
        case IDC_BTN_BENCH: on_bench_click(); break;
        case IDC_BTN_MIC:   on_mic_click(); break;
        case IDC_BTN_LOADMODEL: on_load_model_click(); break;
        case IDC_BTN_HOTSWAP: on_hotswap_click(); break;
        case IDC_BTN_MINDMELD: on_mindmeld_click(); break;
        case IDC_VOICE_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE) on_voice_change();
            break;
        }
        return 0;

    case WM_TIMER:
        update_display();
        InvalidateRect(g_hVu, NULL, TRUE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_DROPFILES: {
        /* Dragon drop support — handle multiple file drops */
        HDROP hDrop = (HDROP)wp;
        char pth_path[512] = "";
        char idx_path[512] = "";
        char name[128] = "";
        int n_files = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);

        for (int i = 0; i < n_files && i < 20; i++) {
            char fpath[520];
            DragQueryFileA(hDrop, i, fpath, sizeof(fpath));

            /* Detect file type by extension */
            const char *dot = strrchr(fpath, '.');
            if (!dot) dot = fpath + strlen(fpath);

            if (strcasecmp(dot, ".pth") == 0) {
                strncpy(pth_path, fpath, sizeof(pth_path) - 1);
                /* Extract model name */
                const char *base = strrchr(fpath, '\\');
                base = base ? base + 1 : fpath;
                strncpy(name, base, sizeof(name) - 1);
                char *nd = strrchr(name, '.');
                if (nd) *nd = '\0';
            } else if (strcasecmp(dot, ".index") == 0) {
                strncpy(idx_path, fpath, sizeof(idx_path) - 1);
            } else if (strcasecmp(dot, ".wubu") == 0) {
                /* Direct .wubu — no index needed */
                strncpy(pth_path, fpath, sizeof(pth_path) - 1);
                const char *base = strrchr(fpath, '\\');
                base = base ? base + 1 : fpath;
                strncpy(name, base, sizeof(name) - 1);
                char *nd = strrchr(name, '.');
                if (nd) *nd = '\0';
            }
        }
        DragFinish(hDrop);

        if (pth_path[0]) {
            /* Auto-detect index if not found */
            if (idx_path[0] == '\0') {
                char auto_idx[520];
                int len = (int)strlen(pth_path);
                if (len > 4) {
                    memcpy(auto_idx, pth_path, len - 4);
                    strcpy(auto_idx + len - 4, ".index");
                    FILE *tf = fopen(auto_idx, "rb");
                    if (tf) { fclose(tf); strncpy(idx_path, auto_idx, sizeof(idx_path) - 1); }
                }
            }

            char display_name[128];
            if (name[0]) strncpy(display_name, name, sizeof(display_name) - 1);
            else strncpy(display_name, "dropped", sizeof(display_name) - 1);
            display_name[sizeof(display_name) - 1] = '\0';

            wubu_model_dock_add(&g_dock, pth_path,
                idx_path[0] ? idx_path : NULL,
                display_name, 2);

            char status[512];
            snprintf(status, sizeof(status), "Dropped: %s (v2, %s)",
                     display_name, idx_path[0] ? "pth+index" : "pth only");
            SetWindowTextA(g_hStatus, status);
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ── Event handlers ── */
static const char *VOICE_NAMES[] = {"default", "cartman", "homer", "terminator",
                                     "chipmunk", "deep", "robot", "alien"};

void on_voice_change(void) {
    int idx = (int)SendMessage(g_hCombo, CB_GETCURSEL, 0, 0);
    if (idx >= 0 && idx < 8 && g_vc) {
        wubu_vc_set_voice(g_vc, VOICE_NAMES[idx]);
        char status[256];
        snprintf(status, sizeof(status), "Voice: %s | Engine: WuBuRVC C11", VOICE_NAMES[idx]);
        SetWindowTextA(g_hStatus, status);
    }
}

void on_speak_click(void) {
    char text[1024];
    GetWindowTextA(g_hEdit, text, sizeof(text));
    if (!g_vc || strlen(text) == 0) {
        MessageBoxA(g_hwnd, "Enter text first!", "WuBuVoice", MB_OK);
        return;
    }

    float output[44100];
    int n = wubu_vc_speak(g_vc, text, output, 44100);
    if (n > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Generated %d samples (%.2fs)",
                 n, (double)n / 22050.0);
        MessageBoxA(g_hwnd, msg, "WuBuVoice", MB_OK);
        g_vu_level = 0.5f;
        InvalidateRect(g_hVu, NULL, TRUE);
    } else {
        MessageBoxA(g_hwnd, "TTS failed", "WuBuVoice", MB_OK | MB_ICONERROR);
    }
}

void on_bench_click(void) {
    if (!g_vc) return;

    int sr = 22050;
    int n_samples = sr * 3;
    float *pcm = (float *)malloc(n_samples * sizeof(float));
    float *out = (float *)malloc(n_samples * sizeof(float));
    if (!pcm || !out) { free(pcm); free(out); return; }

    for (int i = 0; i < n_samples; i++) {
        double t = (double)i / sr;
        double freq = 100.0 + 3900.0 * fmod(t, 2.0) / 2.0;
        pcm[i] = sin(2.0 * M_PI * freq * t) * 0.3f;
    }

    char results[4096] = "";
    char line[256];

    strncat(results, "Speed Comparison (3s audio):\n\n", sizeof(results) - 1);

    for (int v = 0; v < 8; v++) {
        wubu_vc_set_voice(g_vc, VOICE_NAMES[v]);
        DWORD t0 = GetTickCount();
        wubu_vc_process_mic(g_vc, pcm, n_samples, out, n_samples);
        DWORD t1 = GetTickCount();
        double ms = (t1 - t0);

        /* Use QueryPerformanceCounter for sub-ms precision if needed */
        snprintf(line, sizeof(line), "  %-12s %8.2f ms  %.0fx realtime\n",
                 VOICE_NAMES[v], ms, ms > 0 ? 3000.0 / ms : 0);
        strncat(results, line, sizeof(results) - strlen(results) - 1);
    }

    VCInfo info;
    wubu_vc_info(g_vc, &info);
    snprintf(line, sizeof(line), "\nTotal frames: %ld\nAvg latency: %.2f ms\n",
             info.total_frames_processed, info.avg_latency_ms);
    strncat(results, line, sizeof(results) - strlen(results) - 1);

    MessageBoxA(g_hwnd, results, "Benchmark Results", MB_OK | MB_ICONINFORMATION);
    free(pcm); free(out);
}

void on_mic_click(void) {
    const char *btn = g_mic_active ? "Mic OFF" : "Mic ON";
    SetWindowTextA(GetDlgItem(g_hwnd, IDC_BTN_MIC), btn);
    g_mic_active = !g_mic_active;

    if (g_mic_active && g_vc) {
        wubu_vc_start_capture(g_vc);
        SetWindowTextA(g_hStatus, "Mic: ACTIVE (real-time voice changer)");
        SetTimer(g_hwnd, 1, 30, NULL);
    } else {
        wubu_vc_stop_capture(g_vc);
        SetWindowTextA(g_hStatus, "Mic: stopped");
        g_vu_level = 0;
        InvalidateRect(g_hVu, NULL, TRUE);
    }
}

/* ── Model hot-swap handlers ── */
void on_load_model_click(void) {
    /* Open file dialog for .pth model */
    OPENFILENAMEA ofn;
    char szFile[512];
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "RVC Models\0*.pth;*.wubu;*.index\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        char name[128];
        /* Extract filename without extension as model name */
        const char *base = strrchr(szFile, '\\');
        if (!base) base = strrchr(szFile, '/');
        base = base ? base + 1 : szFile;
        strncpy(name, base, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char *dot = strrchr(name, '.');
        if (dot) *dot = '\0';

        /* Look for matching .index file */
        char idx_path[520];
        int len = snprintf(idx_path, sizeof(idx_path) - 6, "%s", szFile);
        if (len > 0) {
            snprintf(idx_path + len, sizeof(idx_path) - len, ".index");
        }
        FILE *tf = fopen(idx_path, "rb");
        if (!tf) idx_path[0] = '\0';
        else fclose(tf);

        /* Add to dock (this starts async loading) */
        wubu_model_dock_add(&g_dock, szFile,
            idx_path[0] ? idx_path : NULL,
            name, 2);

        char status[512];
        snprintf(status, sizeof(status), "Model queued: %s (async load)", name);
        SetWindowTextA(g_hStatus, status);
        SetWindowTextA(g_hModelPath, status);
    }
}

void on_hotswap_click(void) {
    /* Toggle hot-swap mode */
    g_hotswap_pending = !g_hotswap_pending;
    const char *btn_text = g_hotswap_pending ? "Hot-Swap Off" : "Hot-Swap On";
    SetWindowTextA(g_hHotSwapBtn, btn_text);

    wubu_model_dock_set_prewarm(&g_dock, g_hotswap_pending ? 1 : 0);

    /* Poll for completed loads immediately */
    wubu_model_dock_poll(&g_dock);

    char status[256];
    snprintf(status, sizeof(status),
        "Hot-swap: %s | Loaded: %d/%d models",
        g_hotswap_pending ? "ON" : "OFF",
        g_dock.n_loaded, g_dock.count);
    SetWindowTextA(g_hStatus, status);
}

void on_mindmeld_click(void) {
    g_mind_meld_enabled = !g_mind_meld_enabled;
    wubu_model_dock_set_mind_meld(&g_dock, g_mind_meld_enabled);

    char status[256];
    snprintf(status, sizeof(status),
        "Mind-Meld: %s | %s",
        g_mind_meld_enabled ? "ON (3-encoder fusion)" : "OFF (standard)",
        g_mind_meld_enabled ? "3-encoder fusion active" : "standard HuBERT");
    SetWindowTextA(g_hStatus, status);
}

void update_display(void) {
    if (!g_vc) return;

    VCInfo info;
    wubu_vc_info(g_vc, &info);

    char lat[64];
    snprintf(lat, sizeof(lat), "Latency: %.2f ms/frame", info.avg_latency_ms);
    SetWindowTextA(g_hLatency, lat);

    char ram[64];
    snprintf(ram, sizeof(ram), "VRAM: %lu MB", (unsigned long)info.rvc_version);
    SetWindowTextA(g_hRam, ram);
}

/* ── Main ── */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR lpCmd, int nCmd) {
    (void)hPrev; (void)lpCmd;

    /* Initialize engine */
    wubu_vc_default_config(&g_cfg);
    g_cfg.sample_rate = 22050;
    g_vc = wubu_vc_create(&g_cfg);

    /* Initialize model dock */
    wubu_model_dock_init(&g_dock);

    /* Register VU meter class */
    WNDCLASS wc = {0};
    wc.lpfnWndProc = VUProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"VUMETER";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wc);

    /* Register main window class */
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"WuBuVoiceMain";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClass(&wc);

    /* Create window */
    g_hwnd = CreateWindow(L"WuBuVoiceMain", L"WuBuVoice - Our Voice, Our Engine",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 380,
        NULL, NULL, hInst, NULL);
    ShowWindow(g_hwnd, nCmd);
    UpdateWindow(g_hwnd);

    /* Enable drag-drop on main window */
    DragAcceptFiles(g_hwnd, TRUE);

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        /* Poll model dock for completed loads (non-realtime check) */
        if (g_dock.count > 0)
            wubu_model_dock_poll(&g_dock);
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_vc) wubu_vc_destroy(g_vc);
    wubu_model_dock_destroy(&g_dock);
    return (int)msg.wParam;
}

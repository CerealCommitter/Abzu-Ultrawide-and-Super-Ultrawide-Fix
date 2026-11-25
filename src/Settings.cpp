// Launch settings window and settings-file handling for the ABZU fix.
//
// The window runs inside the game process at its entry point (see AbzuFix.cpp), before the
// engine has read any configuration, so whatever is chosen here is what the game starts with.
// It works the same however the game is launched (Steam, Epic, GOG, or the exe directly).
#include "Settings.h"

#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>

#include <algorithm>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

const wchar_t* kSection = L"AbzuFix";
const wchar_t* kGameSection = L"/Script/AbzuGame.AbzuGameUserSettings";

struct Mode {
    int w, h;
};

void DesktopSize(int& w, int& h) {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        w = (int)dm.dmPelsWidth;
        h = (int)dm.dmPelsHeight;
    } else {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }
}

std::vector<Mode> DisplayModes() {
    std::vector<Mode> modes;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 1024 || dm.dmPelsHeight < 720) continue;
        Mode m{(int)dm.dmPelsWidth, (int)dm.dmPelsHeight};
        bool dup = false;
        for (const Mode& x : modes) dup |= (x.w == m.w && x.h == m.h);
        if (!dup) modes.push_back(m);
    }
    std::sort(modes.begin(), modes.end(), [](const Mode& a, const Mode& b) {
        return a.w * a.h != b.w * b.h ? a.w * a.h > b.w * b.h : a.w > b.w;
    });
    return modes;
}

// Picture-shape choices offered in the window.
struct Shape {
    const wchar_t* value;
    const wchar_t* label;
};
const Shape kShapes[] = {
    {L"0", L"Fill the whole screen"},
    {L"21:9", L"21:9 with black bars at the sides"},
    {L"16:9", L"16:9 with black bars at the sides"},
    {L"4:3", L"4:3 with black bars at the sides"},
};

// ---- the window ------------------------------------------------------------------------
enum { IdRes = 101, IdShape, IdShow, IdPlay, IdQuit };

struct WindowState {
    FixSettings* s;
    std::vector<Mode> modes;
    int nativeW, nativeH;
    bool customShape;  // ini holds a shape not in kShapes; kept as the last list item
    bool play = false;
    bool done = false;
    HWND res = nullptr, shape = nullptr, show = nullptr;
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wp) == IdPlay || LOWORD(wp) == IDOK) {
                // Resolution: item 0 = native, then the mode list.
                int r = (int)SendMessageW(st->res, CB_GETCURSEL, 0, 0);
                if (r <= 0) {
                    st->s->resX = st->s->resY = 0;
                } else {
                    st->s->resX = st->modes[r - 1].w;
                    st->s->resY = st->modes[r - 1].h;
                }
                int sh = (int)SendMessageW(st->shape, CB_GETCURSEL, 0, 0);
                if (sh >= 0 && sh < (int)(sizeof(kShapes) / sizeof(kShapes[0])))
                    lstrcpynW(st->s->aspect, kShapes[sh].value, 32);
                st->s->showWindow = SendMessageW(st->show, BM_GETCHECK, 0, 0) == BST_CHECKED;
                st->play = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IdQuit || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) st->done = true;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
          HWND parent, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

bool ShowWindowAndWait(FixSettings& s) {
    WindowState st;
    st.s = &s;
    st.modes = DisplayModes();
    DesktopSize(st.nativeW, st.nativeH);

    NONCLIENTMETRICSW ncm = {sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = -15;
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_SEMIBOLD;
    bold.lfHeight = -20;
    HFONT titleFont = CreateFontIndirectW(&bold);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AbzuFixLaunchSettings";
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    const int W = 460, H = 330;
    RECT rc = {0, 0, W, H};
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    AdjustWindowRect(&rc, style, FALSE);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
                                L"ABZ\x00DB - Screen Settings", style,
                                (GetSystemMetrics(SM_CXSCREEN) - ww) / 2,
                                (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, nullptr,
                                nullptr, wc.hInstance, nullptr);
    if (!hwnd) return true;  // can't show a window: just start the game
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);

    const int L = 24, CW = W - 2 * L;
    Make(L"STATIC", L"Abzu Screen Resolution Fix", 0, L, 18, CW, 28, hwnd, 0, titleFont);

    Make(L"STATIC", L"Resolution", 0, L, 62, CW, 20, hwnd, 0, font);
    st.res = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, L, 84, CW, 400,
                  hwnd, IdRes, font);
    wchar_t buf[96];
    swprintf_s(buf, L"Native (%d x %d)", st.nativeW, st.nativeH);
    SendMessageW(st.res, CB_ADDSTRING, 0, (LPARAM)buf);
    int sel = 0;
    for (size_t i = 0; i < st.modes.size(); ++i) {
        const Mode& m = st.modes[i];
        swprintf_s(buf, L"%d x %d", m.w, m.h);
        SendMessageW(st.res, CB_ADDSTRING, 0, (LPARAM)buf);
        if (s.resX == m.w && s.resY == m.h) sel = (int)i + 1;
    }
    SendMessageW(st.res, CB_SETCURSEL, sel, 0);

    Make(L"STATIC", L"Picture shape", 0, L, 128, CW, 20, hwnd, 0, font);
    st.shape = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, L, 150, CW,
                    300, hwnd, IdShape, font);
    int shapeSel = -1;
    const int nShapes = (int)(sizeof(kShapes) / sizeof(kShapes[0]));
    for (int i = 0; i < nShapes; ++i) {
        SendMessageW(st.shape, CB_ADDSTRING, 0, (LPARAM)kShapes[i].label);
        if (ParseAspect(s.aspect) == ParseAspect(kShapes[i].value)) shapeSel = i;
    }
    if (shapeSel < 0) {  // a custom value from the ini (e.g. 64:27): keep it selectable
        swprintf_s(buf, L"%s with black bars (from AbzuFix.ini)", s.aspect);
        SendMessageW(st.shape, CB_ADDSTRING, 0, (LPARAM)buf);
        shapeSel = nShapes;
    }
    SendMessageW(st.shape, CB_SETCURSEL, shapeSel, 0);

    st.show = Make(L"BUTTON", L"Show this window when the game starts",
                   BS_AUTOCHECKBOX | WS_TABSTOP, L, 196, CW, 24, hwnd, IdShow, font);
    SendMessageW(st.show, BM_SETCHECK, s.showWindow ? BST_CHECKED : BST_UNCHECKED, 0);
    Make(L"STATIC", L"Hold Shift while the game starts to bring it back.", 0, L + 22, 220,
         CW - 22, 20, hwnd, 0, font);

    Make(L"BUTTON", L"Quit", WS_TABSTOP, W - L - 110 - 12 - 110, H - 24 - 36, 110, 36, hwnd,
         IdQuit, font);
    HWND play = Make(L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, W - L - 110,
                     H - 24 - 36, 110, 36, hwnd, IdPlay, font);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(play);
    // Drop "always on top" once shown; it was only there so the window isn't hidden behind
    // the launcher on start.
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    MSG m;
    while (!st.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    DeleteObject(font);
    DeleteObject(titleFont);
    return st.play;
}

}  // namespace

// Writes the resolution into the game's own settings file, keeping everything else in it.
void ApplyResolutionToGame(const FixSettings& s) {
    int w = s.resX, h = s.resY;
    if (w <= 0 || h <= 0) DesktopSize(w, h);

    wchar_t dir[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) return;
    lstrcatW(dir, L"\\AbzuGame\\Saved\\Config\\WindowsNoEditor");
    SHCreateDirectoryExW(nullptr, dir, nullptr);
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\GameUserSettings.ini", dir);

    wchar_t ws[16], hs[16];
    swprintf_s(ws, L"%d", w);
    swprintf_s(hs, L"%d", h);
    WritePrivateProfileStringW(kGameSection, L"ResolutionSizeX", ws, ini);
    WritePrivateProfileStringW(kGameSection, L"ResolutionSizeY", hs, ini);
    WritePrivateProfileStringW(kGameSection, L"LastUserConfirmedResolutionSizeX", ws, ini);
    WritePrivateProfileStringW(kGameSection, L"LastUserConfirmedResolutionSizeY", hs, ini);
    WritePrivateProfileStringW(kGameSection, L"DesiredScreenWidth", ws, ini);
    WritePrivateProfileStringW(kGameSection, L"DesiredScreenHeight", hs, ini);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini);  // flush
}

float ParseAspect(const wchar_t* text) {
    double w = 0, h = 0;
    float a = 0.0f;
    if (swscanf_s(text, L"%lf : %lf", &w, &h) == 2 && w > 0 && h > 0)
        a = (float)(w / h);
    else if (swscanf_s(text, L"%lf", &w) == 1 && w > 0)
        a = (float)w;
    return (a >= 1.0f && a <= 10.0f) ? a : 0.0f;
}

void LoadFixSettings(const wchar_t* ini, FixSettings& s) {
    s.enabled = GetPrivateProfileIntW(kSection, L"Enabled", 1, ini) != 0;
    s.log = GetPrivateProfileIntW(kSection, L"Log", 0, ini) != 0;
    s.showWindow = GetPrivateProfileIntW(kSection, L"ShowSettingsAtLaunch", 1, ini) != 0;
    wchar_t v[64] = {};
    GetPrivateProfileStringW(kSection, L"Resolution", L"native", v, 64, ini);
    s.resX = s.resY = 0;
    int w = 0, h = 0;
    if (swscanf_s(v, L"%d x %d", &w, &h) == 2 && w >= 640 && h >= 480) {
        s.resX = w;
        s.resY = h;
    }
    GetPrivateProfileStringW(kSection, L"AspectRatio", L"0", s.aspect, 32, ini);
}

void SaveFixSettings(const wchar_t* ini, const FixSettings& s) {
    wchar_t v[32];
    if (s.resX > 0)
        swprintf_s(v, L"%dx%d", s.resX, s.resY);
    else
        lstrcpyW(v, L"native");
    WritePrivateProfileStringW(kSection, L"Resolution", v, ini);
    WritePrivateProfileStringW(kSection, L"AspectRatio", s.aspect, ini);
    WritePrivateProfileStringW(kSection, L"ShowSettingsAtLaunch", s.showWindow ? L"1" : L"0", ini);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini);
}

bool RunLaunchSettings(const wchar_t* ini, FixSettings& s) {
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (s.showWindow || shift) {
        if (!ShowWindowAndWait(s)) return false;  // Quit
        SaveFixSettings(ini, s);
    }
    ApplyResolutionToGame(s);
    return true;
}

// Launch settings window and settings-file handling for the ABZU fix.
//
// The window runs inside the game process at its entry point (see AbzuFix.cpp), before the
// engine has read any configuration, so whatever is chosen here is what the game starts with.
// It works the same however the game is launched (Steam, Epic, GOG, or the exe directly).
#include "Settings.h"

#include "LauncherUI.h"

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

// The DLL holding the logo resource (this module, not the game exe).
HMODULE ThisModule() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisModule), &m);
    return m;
}

bool ShowWindowAndWait(FixSettings& s) {
    const std::vector<Mode> modes = DisplayModes();
    int nativeW = 0, nativeH = 0;
    DesktopSize(nativeW, nativeH);
    wchar_t buf[96];

    LauncherSpec spec;
    spec.windowTitle = L"ABZ\x00DB - Screen Settings";
    spec.gameTitle = L"ABZ\x00DB";
    spec.subtitle = L"Screen Resolution Fix";
    spec.checkboxText = L"Show these settings every time the game starts";
    spec.hintText = L"Turned off? Hold Shift while starting the game to see these settings again.";
    spec.checked = s.showWindow;

    // Resolution: item 0 = native, then every mode the monitor supports.
    LauncherChoice res;
    res.label = L"Resolution";
    swprintf_s(buf, L"Native (%d x %d)", nativeW, nativeH);
    res.items.push_back(buf);
    for (size_t i = 0; i < modes.size(); ++i) {
        swprintf_s(buf, L"%d x %d", modes[i].w, modes[i].h);
        res.items.push_back(buf);
        if (s.resX == modes[i].w && s.resY == modes[i].h) res.selected = (int)i + 1;
    }
    spec.choices.push_back(res);

    // Picture shape, plus a custom value from the ini (e.g. 64:27) if one is set.
    LauncherChoice shape;
    shape.label = L"Picture shape";
    const int nShapes = (int)(sizeof(kShapes) / sizeof(kShapes[0]));
    shape.selected = -1;
    for (int i = 0; i < nShapes; ++i) {
        shape.items.push_back(kShapes[i].label);
        if (ParseAspect(s.aspect) == ParseAspect(kShapes[i].value)) shape.selected = i;
    }
    if (shape.selected < 0) {
        swprintf_s(buf, L"%s with black bars at the sides", s.aspect);
        shape.items.push_back(buf);
        shape.selected = nShapes;
    }
    spec.choices.push_back(shape);

    if (!ShowLauncher(ThisModule(), spec)) return false;

    const int r = spec.choices[0].selected;
    if (r <= 0 || r > (int)modes.size()) {
        s.resX = s.resY = 0;
    } else {
        s.resX = modes[r - 1].w;
        s.resY = modes[r - 1].h;
    }
    const int sh = spec.choices[1].selected;
    if (sh >= 0 && sh < nShapes) lstrcpynW(s.aspect, kShapes[sh].value, 32);
    s.showWindow = spec.checked;
    return true;
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

void DesktopResolution(int& w, int& h) { DesktopSize(w, h); }

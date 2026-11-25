// Abzu Ultrawide & Super-Ultrawide Fix
//
// ABZU already does Hor+ (keeps the vertical view fixed and widens the sides): its camera code
// takes a vertical FOV (58 degrees in normal play) and computes the horizontal FOV as
//     FOV = 2*atan(tan(VerticalFOV/2) * aspect)
// but then clamps the result to 100 degrees. On screens wider than about 19.4:9 that clamp
// kicks in, the horizontal FOV stops growing, and the engine (which keeps horizontal FOV) has to
// crop the top and bottom instead.
//
// This DLL hooks that camera function. When the screen is wider than 16:9 it replaces the fixed
// 100-degree cap with the same cap converted from 16:9 to the current aspect ratio, so every shot
// keeps exactly the vertical framing it has at 16:9. At 16:9 and narrower nothing is changed.
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Settings.h"

namespace {

// ---- settings / logging ----------------------------------------------------------------
wchar_t g_dir[MAX_PATH];  // folder holding this DLL, with trailing backslash
bool g_enabled = true;
bool g_log = false;
float g_boxAspect = 0.0f;  // AspectRatio setting: 0 = use the full screen width
SRWLOCK g_logLock = SRWLOCK_INIT;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    wchar_t path[MAX_PATH];
    lstrcpyW(path, g_dir);
    lstrcatW(path, L"AbzuFix.log");
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)strlen(buf);
    AcquireSRWLockExclusive(&g_logLock);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, buf, (DWORD)n, &w, nullptr);
        WriteFile(h, "\r\n", 2, &w, nullptr);
        CloseHandle(h);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

wchar_t g_ini[MAX_PATH];
FixSettings g_settings;

void ApplySettings() {
    g_enabled = g_settings.enabled;
    g_log = g_settings.log;
    g_boxAspect = ParseAspect(g_settings.aspect);
}

// ---- launch window at the game's entry point ---------------------------------------------
// DllMain runs under the loader lock, where showing a window isn't safe. Instead the exe's
// entry point is redirected here: the settings window runs on the main thread before the
// game's own startup code (and before the engine reads its config), then the original entry
// bytes are restored and the game starts normally.
using EntryFn = int(__stdcall*)(void* peb);
uint8_t* g_entry = nullptr;
uint8_t g_entryBytes[14];

// ---- startup focus (USER32 imports of the game exe) ---------------------------------------
// When the game starts without the settings window (e.g. straight from Steam), Windows often
// refuses its request for focus, because the request doesn't follow a click in the game. A
// fullscreen Unreal window without focus minimizes itself, so the game started minimized and
// played its intro off-screen. The game's own SetForegroundWindow / ShowWindow calls are
// redirected here: during startup only, a refused focus request is retried with the standard
// thread-input hand-off, and a minimize caused by the missing focus is turned into a normal
// show. Once the game has had focus, both calls are passed through untouched, so alt-tabbing
// away later works as before.
using SetFgFn = BOOL(WINAPI*)(HWND);
using ShowWinFn = BOOL(WINAPI*)(HWND, int);
SetFgFn g_realSetForeground = nullptr;
ShowWinFn g_realShowWindow = nullptr;
volatile bool g_startupDone = false;

bool IsGameWindow(HWND w) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId()) return false;
    wchar_t cls[32];
    return GetClassNameW(w, cls, 32) && lstrcmpW(cls, L"UnrealWindow") == 0;
}

bool TakeFocus(HWND w) {
    if (GetForegroundWindow() == w) return true;
    HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD me = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
    BringWindowToTop(w);
    g_realSetForeground(w);
    if (attached) AttachThreadInput(me, fgThread, FALSE);
    if (GetForegroundWindow() == w) return true;

    // Still refused: Windows only lets the program you launched take focus, and Steam starts
    // the game through AbzuGame.exe, so the real game process doesn't qualify. A synthetic Alt
    // tap counts as fresh input and lifts that lock for the next focus request.
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_MENU;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in[0], sizeof(INPUT));
    g_realSetForeground(w);
    SendInput(1, &in[1], sizeof(INPUT));
    return GetForegroundWindow() == w;
}

BOOL WINAPI SetForegroundHook(HWND w) {
    BOOL ok = g_realSetForeground(w);
    if (g_startupDone || !IsGameWindow(w)) return ok;
    if (GetForegroundWindow() != w) {
        ok = TakeFocus(w);
        Log("startup focus: game asked for focus; %s", ok ? "granted" : "still refused");
    }
    if (GetForegroundWindow() == w) g_startupDone = true;
    return ok;
}

BOOL WINAPI ShowWindowHook(HWND w, int cmd) {
    const bool minimize = cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED ||
                          cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE;
    if (!g_startupDone && minimize && IsGameWindow(w)) {
        if (TakeFocus(w)) {
            Log("startup focus: kept the game window from minimizing (cmd %d)", cmd);
            g_startupDone = true;
            return g_realShowWindow(w, SW_SHOW);
        }
        wchar_t owner[MAX_PATH] = L"?";
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            DWORD len = MAX_PATH;
            QueryFullProcessImageNameW(h, 0, owner, &len);
            CloseHandle(h);
        }
        Log("startup focus: couldn't get focus (focus held by %ls); letting the game minimize",
            owner);
    }
    BOOL r = g_realShowWindow(w, cmd);
    if (!g_startupDone && IsGameWindow(w) && GetForegroundWindow() == w) g_startupDone = true;
    return r;
}

// Points one of the exe's imports at our function; returns the original.
void* PatchImport(const char* dll, const char* name, void* hook) {
    uint8_t* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(exe + dir.VirtualAddress);
         imp->Name; ++imp) {
        if (lstrcmpiA(reinterpret_cast<char*>(exe + imp->Name), dll) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(
            exe + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto* addrs = reinterpret_cast<IMAGE_THUNK_DATA64*>(exe + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addrs) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(exe + names->u1.AddressOfData);
            if (lstrcmpA(reinterpret_cast<char*>(byName->Name), name) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&addrs->u1.Function, sizeof(void*), PAGE_READWRITE, &old))
                return nullptr;
            void* orig = reinterpret_cast<void*>(addrs->u1.Function);
            addrs->u1.Function = reinterpret_cast<ULONGLONG>(hook);
            VirtualProtect(&addrs->u1.Function, sizeof(void*), old, &old);
            return orig;
        }
    }
    return nullptr;
}

void InstallFocusHooks() {
    g_realSetForeground = reinterpret_cast<SetFgFn>(
        PatchImport("USER32.dll", "SetForegroundWindow", reinterpret_cast<void*>(&SetForegroundHook)));
    g_realShowWindow = reinterpret_cast<ShowWinFn>(
        PatchImport("USER32.dll", "ShowWindow", reinterpret_cast<void*>(&ShowWindowHook)));
    if (!g_realSetForeground) g_realSetForeground = SetForegroundWindow;
    if (!g_realShowWindow) g_realShowWindow = ShowWindow;
    Log("startup focus hooks installed");
}

int __stdcall EntryHook(void* peb) {
    DWORD old;
    VirtualProtect(g_entry, sizeof(g_entryBytes), PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_entry, g_entryBytes, sizeof(g_entryBytes));
    VirtualProtect(g_entry, sizeof(g_entryBytes), old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof(g_entryBytes));

    if (!RunLaunchSettings(g_ini, g_settings)) ExitProcess(0);  // player chose Quit
    ApplySettings();
    return reinterpret_cast<EntryFn>(g_entry)(peb);
}

// ---- the game's camera FOV function -------------------------------------------------------
// void CameraApplyFOV(Camera* self, const FMinimalViewInfo* in, FMinimalViewInfo* out)
//   aspect   = self->GetViewAspect()         (sub at +0x1D; 1.0 if no viewport)
//   out->FOV = min(2*deg(atan(tan(rad(in->FOV)/2) * aspect)), 100)     in->FOV is vertical
constexpr size_t kFovOffset = 0x18;     // FMinimalViewInfo::FOV
constexpr size_t kAspectOffset = 0x28;  // FMinimalViewInfo::AspectRatio
constexpr size_t kFlagsOffset = 0x2C;   // bit 0 = FMinimalViewInfo::bConstrainAspectRatio
using ApplyFovFn = void(__fastcall*)(void* self, const uint8_t* in, uint8_t* out);
using AspectScaleFn = float(__fastcall*)(void* self);
ApplyFovFn g_original = nullptr;
AspectScaleFn g_aspectScale = nullptr;
float g_gameCap = 100.0f;  // read from the game's own code at install time

constexpr double kPi = 3.14159265358979323846;
inline double HalfTan(double deg) { return tan(deg * kPi / 360.0); }
inline double FromHalfTan(double t) { return atan(t) * 360.0 / kPi; }

void __fastcall Hook(void* self, const uint8_t* in, uint8_t* out) {
    const float baseFov = *reinterpret_cast<const float*>(in + kFovOffset);  // before in==out writes
    g_original(self, in, out);
    if (!g_enabled) return;

    constexpr float kRef = 16.0f / 9.0f;
    const float screen = g_aspectScale(self);
    if (!(screen > 0.5f) || !(screen < 16.0f) || !(baseFov > 0.0f) || !(baseFov < 179.0f))
        return;

    // Black-bar mode: if a narrower AspectRatio is set, draw the picture as if the monitor were
    // that shape. The engine centres it and fills the sides with black (pillarbox).
    const bool box = g_boxAspect > 0.0f && g_boxAspect < screen * 0.999f;
    const float aspect = box ? g_boxAspect : screen;

    float& outFov = *reinterpret_cast<float*>(out + kFovOffset);
    const float gameFov = outFov;
    float fov = gameFov;
    if (box) {
        // What the game itself would compute on a screen of this shape.
        const double horPlus = FromHalfTan(HalfTan(baseFov) * aspect);
        fov = (float)(horPlus < g_gameCap ? horPlus : g_gameCap);
    }
    if (aspect > kRef * 1.0001f) {
        // Wider than 16:9: same formula as the game, with the 100-degree cap (defined for 16:9)
        // converted to this aspect so it limits the same vertical view it limits at 16:9.
        const double horPlus = FromHalfTan(HalfTan(baseFov) * aspect);
        const double cap = FromHalfTan(HalfTan(g_gameCap) * (aspect / kRef));
        fov = (float)(horPlus < cap ? horPlus : cap);
    }
    // At 16:9 or narrower with no black bars, fov == gameFov: nothing is changed.

    static float lastLogged = -1.0f;
    if (g_log && fabsf(fov - lastLogged) > 0.5f) {
        lastLogged = fov;
        Log("screen=%.4f picture=%.4f vfov=%.3f game=%.3f fixed=%.3f", screen, aspect, baseFov,
            gameFov, fov);
    }
    outFov = fov;
    if (box) {
        *reinterpret_cast<float*>(out + kAspectOffset) = aspect;
        out[kFlagsOffset] |= 1;  // bConstrainAspectRatio
    }
}

// ---- hook installation -----------------------------------------------------------------
// Start of the camera FOV function; 0x100 = wildcard (the call's rel32).
const uint16_t kSig[] = {
    0x48, 0x8B, 0xC4,                          // mov  rax,rsp
    0x48, 0x89, 0x58, 0x08,                    // mov  [rax+8],rbx
    0x57,                                      // push rdi
    0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,  // sub  rsp,0A0h
    0x0F, 0x29, 0x70, 0xE8,                    // movaps [rax-18h],xmm6
    0x49, 0x8B, 0xF8,                          // mov  rdi,r8
    0x0F, 0x29, 0x78, 0xD8,                    // movaps [rax-28h],xmm7
    0x48, 0x8B, 0xDA,                          // mov  rbx,rdx
    0xE8, 0x100, 0x100, 0x100, 0x100,          // call GetAspectScale
    0x0F, 0x10, 0x73, 0x10,                    // movups xmm6,[rbx+10h]
    0x0F, 0x28, 0xF8,                          // movaps xmm7,xmm0
};
constexpr size_t kCallOffset = 29;
constexpr size_t kStolen = 15;  // mov rax,rsp / mov [rax+8],rbx / push rdi / sub rsp,0A0h
// Tail of the same function: mulss xmm0,[2.0] ; minss xmm0,[cap] ; movss [rdi+18h],xmm0
const uint8_t kTail[] = {0xF3, 0x0F, 0x5D, 0x05};  // minss xmm0,[rip+disp32]
constexpr size_t kTailOffset = 0x130;

bool Matches(const uint8_t* p) {
    for (size_t i = 0; i < sizeof(kSig) / sizeof(kSig[0]); ++i)
        if (kSig[i] < 0x100 && p[i] != kSig[i]) return false;
    return true;
}

uint8_t* FindTarget(uint8_t* base) {
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    uint8_t* found = nullptr;
    int hits = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* p = base + sec->VirtualAddress;
        size_t n = sec->Misc.VirtualSize;
        for (size_t j = 0; j + kTailOffset + 16 <= n; ++j) {
            if (p[j] != 0x48 || !Matches(p + j)) continue;
            if (memcmp(p + j + kTailOffset, kTail, sizeof(kTail)) != 0) continue;
            found = p + j;
            ++hits;
        }
    }
    return hits == 1 ? found : nullptr;  // refuse if missing or ambiguous
}

void WriteAbsJump(uint8_t* at, const void* to) {
    at[0] = 0xFF;  // jmp qword ptr [rip+0]
    at[1] = 0x25;
    *reinterpret_cast<uint32_t*>(at + 2) = 0;
    *reinterpret_cast<const void**>(at + 6) = to;
}

// ---- resolution requests (FSystemResolution::RequestResolutionChange) --------------------
using ResChangeFn = void(__fastcall*)(int x, int y, int mode);
ResChangeFn g_resOriginal = nullptr;

void __fastcall ResChangeHook(int x, int y, int mode) {
    if (g_log) {
        void* frames[8] = {};
        const USHORT n = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
        const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        char stack[256] = "";
        for (USHORT i = 0; i < n; ++i) {
            char part[24];
            sprintf_s(part, " +0x%llx", (unsigned long long)(reinterpret_cast<uintptr_t>(frames[i]) - exe));
            strcat_s(stack, part);
        }
        Log("RequestResolutionChange(%d, %d, mode=%d) from%s", x, y, mode, stack);
    }
    g_resOriginal(x, y, mode);
}

const uint8_t kResSig[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,  // mov [rsp+8],rbx
    0x48, 0x89, 0x74, 0x24, 0x10,  // mov [rsp+10h],rsi
    0x48, 0x89, 0x7C, 0x24, 0x18,  // mov [rsp+18h],rdi
    0x4C, 0x89, 0x74, 0x24, 0x20,  // mov [rsp+20h],r14
    0x55,                          // push rbp
    0x48, 0x8B, 0xEC,              // mov rbp,rsp
    0x48, 0x83, 0xEC, 0x60,        // sub rsp,60h
    0x33, 0xDB,                    // xor ebx,ebx
};
const uint8_t kResSig2[] = {0x8B, 0xF2, 0x48, 0x89, 0x5D, 0xE8, 0x44, 0x8B, 0xF1, 0x45, 0x85, 0xC0};
constexpr size_t kResSig2Offset = 0x29;
constexpr size_t kResStolen = 20;  // the four mov [rsp+N],reg instructions

void InstallResHook() {
    uint8_t* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    uint8_t* found = nullptr;
    int hits = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* p = exe + sec->VirtualAddress;
        const size_t n = sec->Misc.VirtualSize;
        for (size_t j = 0; j + 64 <= n; ++j) {
            if (p[j] != 0x48 || memcmp(p + j, kResSig, sizeof(kResSig)) != 0) continue;
            if (memcmp(p + j + kResSig2Offset, kResSig2, sizeof(kResSig2)) != 0) continue;
            found = p + j;
            ++hits;
        }
    }
    if (hits != 1) {
        Log("RequestResolutionChange not found (%d matches)", hits);
        return;
    }
    auto* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return;
    memcpy(tramp, found, kResStolen);
    WriteAbsJump(tramp + kResStolen, found + kResStolen);
    g_resOriginal = reinterpret_cast<ResChangeFn>(tramp);
    DWORD old;
    if (!VirtualProtect(found, kResStolen, PAGE_EXECUTE_READWRITE, &old)) return;
    WriteAbsJump(found, reinterpret_cast<void*>(&ResChangeHook));
    for (size_t k = 14; k < kResStolen; ++k) found[k] = 0x90;
    VirtualProtect(found, kResStolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), found, kResStolen);
    Log("hooked RequestResolutionChange at exe+0x%llx", (unsigned long long)(found - exe));
}

// ---- boot resolution (UGameEngine::DetermineGameWindowResolution) ------------------------
// Unreal replaces any fullscreen resolution that isn't smaller than the monitor's "native" size
// with that native size, and it reads "native" from the monitor's EDID. Some monitors report a
// smaller size there than they support (a 5120x1440 monitor reporting 3840x1080), so the game
// boots at the wrong size and only switches to the right one after the intro, blanking the
// screen during the switch. This puts back the resolution chosen in the settings window
// (native = the desktop's current resolution) whenever the game asks for fullscreen.
using DetermineResFn = void(__fastcall*)(int* x, int* y, int* mode);
DetermineResFn g_determineOriginal = nullptr;

void __fastcall DetermineResHook(int* x, int* y, int* mode) {
    const int inX = *x, inY = *y;
    g_determineOriginal(x, y, mode);
    if (*mode == 2) return;  // windowed: leave the game's choice alone
    int w = g_settings.resX, h = g_settings.resY;
    if (w <= 0 || h <= 0) DesktopResolution(w, h);
    if (w <= 0 || h <= 0 || (*x == w && *y == h)) return;
    Log("boot resolution: asked %dx%d, engine chose %dx%d (mode %d) -> %dx%d", inX, inY, *x, *y,
        *mode, w, h);
    *x = w;
    *y = h;
}

const uint8_t kDetSigA[] = {0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x49, 0x8D, 0x6B, 0xA1, 0x48,
                            0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05};
const uint8_t kDetSigB[] = {0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0x17, 0x49, 0x89,
                            0x73, 0x20, 0x49, 0x8B, 0xF0, 0x49, 0x89, 0x7B};
constexpr size_t kDetSigBOffset = 23;  // after the 4-byte address following kDetSigA
// mov r11,rsp / push rbp / push rbx / lea rbp,[r11-5Fh] / sub rsp,0B8h
constexpr size_t kDetStolen = 16;

uint8_t* FindPattern(uint8_t* exe, const uint8_t* a, size_t na, const uint8_t* b, size_t nb,
                     size_t bOffset) {
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    uint8_t* found = nullptr;
    int hits = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* p = exe + sec->VirtualAddress;
        const size_t n = sec->Misc.VirtualSize;
        for (size_t j = 0; j + bOffset + nb <= n; ++j) {
            if (p[j] != a[0] || memcmp(p + j, a, na) != 0) continue;
            if (memcmp(p + j + bOffset, b, nb) != 0) continue;
            found = p + j;
            ++hits;
        }
    }
    return hits == 1 ? found : nullptr;
}

bool HookAt(uint8_t* target, size_t stolen, void* hook, void** original) {
    auto* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return false;
    memcpy(tramp, target, stolen);
    WriteAbsJump(tramp + stolen, target + stolen);
    *original = tramp;
    DWORD old;
    if (!VirtualProtect(target, stolen, PAGE_EXECUTE_READWRITE, &old)) return false;
    WriteAbsJump(target, hook);
    for (size_t k = 14; k < stolen; ++k) target[k] = 0x90;
    VirtualProtect(target, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, stolen);
    return true;
}

void InstallBootResolutionHook() {
    uint8_t* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    uint8_t* t = FindPattern(exe, kDetSigA, sizeof(kDetSigA), kDetSigB, sizeof(kDetSigB),
                             kDetSigBOffset);
    if (!t) {
        Log("DetermineGameWindowResolution not found - boot resolution not corrected");
        return;
    }
    if (HookAt(t, kDetStolen, reinterpret_cast<void*>(&DetermineResHook),
               reinterpret_cast<void**>(&g_determineOriginal)))
        Log("hooked DetermineGameWindowResolution at exe+0x%llx",
            (unsigned long long)(t - exe));
}

void InstallHook() {
    uint8_t* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    uint8_t* target = FindTarget(exe);
    if (!target) {
        Log("camera FOV function not found (different game version?) - fix not applied");
        return;
    }
    // Resolve the game's aspect-scale getter and its FOV cap from the function itself.
    const int32_t callRel = *reinterpret_cast<int32_t*>(target + kCallOffset + 1);
    g_aspectScale = reinterpret_cast<AspectScaleFn>(target + kCallOffset + 5 + callRel);
    const int32_t capRel = *reinterpret_cast<int32_t*>(target + kTailOffset + 4);
    const float cap = *reinterpret_cast<float*>(target + kTailOffset + 8 + capRel);
    if (!(cap > 30.0f && cap < 170.0f)) {
        Log("unexpected FOV cap %.3f - fix not applied", cap);
        return;
    }
    g_gameCap = cap;

    auto* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return;
    memcpy(tramp, target, kStolen);
    WriteAbsJump(tramp + kStolen, target + kStolen);
    g_original = reinterpret_cast<ApplyFovFn>(tramp);

    DWORD old;
    if (!VirtualProtect(target, kStolen, PAGE_EXECUTE_READWRITE, &old)) return;
    WriteAbsJump(target, reinterpret_cast<void*>(&Hook));
    target[14] = 0x90;  // pad the 15th stolen byte
    VirtualProtect(target, kStolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, kStolen);
    Log("hooked camera FOV function at exe+0x%llx, game cap %.1f deg",
        (unsigned long long)(target - exe), cap);
}

bool IsGameExe() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return lstrcmpiW(name, L"AbzuGame-Win64-Shipping.exe") == 0;
}

bool InstallEntryHook() {
    uint8_t* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    if (!nt->OptionalHeader.AddressOfEntryPoint) return false;
    g_entry = exe + nt->OptionalHeader.AddressOfEntryPoint;
    memcpy(g_entryBytes, g_entry, sizeof(g_entryBytes));
    DWORD old;
    if (!VirtualProtect(g_entry, sizeof(g_entryBytes), PAGE_EXECUTE_READWRITE, &old))
        return false;
    WriteAbsJump(g_entry, reinterpret_cast<void*>(&EntryHook));
    VirtualProtect(g_entry, sizeof(g_entryBytes), old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof(g_entryBytes));
    return true;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) slash[1] = 0;
        if (IsGameExe()) {
            lstrcpyW(g_ini, g_dir);
            lstrcatW(g_ini, L"AbzuFix.ini");
            LoadFixSettings(g_ini, g_settings);
            ApplySettings();
            Log("---- Abzu Ultrawide & Super-Ultrawide Fix loaded (Enabled=%d, AspectRatio=%.4f) ----",
                g_enabled ? 1 : 0, g_boxAspect);
            if (g_enabled) InstallHook();
            InstallBootResolutionHook();
            InstallFocusHooks();
            if (g_log) InstallResHook();  // troubleshooting: log every resolution request
            if (!InstallEntryHook()) {
                // No launch window possible (no windows under the loader lock): still apply
                // the saved resolution so the game doesn't fall back to its own pick.
                ApplyResolutionToGame(g_settings);
            }
        }
    }
    return TRUE;
}

// Abzu Screen Resolution Fix
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

namespace {

// ---- settings / logging ----------------------------------------------------------------
wchar_t g_dir[MAX_PATH];  // folder holding this DLL, with trailing backslash
bool g_enabled = true;
bool g_log = false;
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

void LoadSettings() {
    wchar_t ini[MAX_PATH];
    lstrcpyW(ini, g_dir);
    lstrcatW(ini, L"AbzuFix.ini");
    g_enabled = GetPrivateProfileIntW(L"AbzuFix", L"Enabled", 1, ini) != 0;
    g_log = GetPrivateProfileIntW(L"AbzuFix", L"Log", 0, ini) != 0;
}

// ---- the game's camera FOV function -------------------------------------------------------
// void CameraApplyFOV(Camera* self, const FMinimalViewInfo* in, FMinimalViewInfo* out)
//   aspect   = self->GetViewAspect()         (sub at +0x1D; 1.0 if no viewport)
//   out->FOV = min(2*deg(atan(tan(rad(in->FOV)/2) * aspect)), 100)     in->FOV is vertical
constexpr size_t kFovOffset = 0x18;  // FMinimalViewInfo::FOV
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
    const float aspect = g_aspectScale(self);
    // Only screens wider than 16:9; 16:9 and narrower keep the game's own result untouched.
    if (!(aspect > kRef * 1.0001f) || !(aspect < 16.0f) || !(baseFov > 0.0f) ||
        !(baseFov < 179.0f))
        return;

    // Same formula as the game; the 100-degree cap (defined for 16:9) is converted to this
    // aspect ratio so it limits the same vertical view it limits at 16:9.
    const double horPlus = FromHalfTan(HalfTan(baseFov) * aspect);
    const double cap = FromHalfTan(HalfTan(g_gameCap) * (aspect / kRef));
    const float fixedFov = (float)(horPlus < cap ? horPlus : cap);
    float& outFov = *reinterpret_cast<float*>(out + kFovOffset);

    static float lastLogged = -1.0f;
    if (g_log && fabsf(fixedFov - lastLogged) > 0.5f) {
        lastLogged = fixedFov;
        Log("aspect=%.4f vfov=%.3f game=%.3f fixed=%.3f", aspect, baseFov, outFov, fixedFov);
    }
    outFov = fixedFov;
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

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) slash[1] = 0;
        if (IsGameExe()) {
            LoadSettings();
            Log("---- Abzu Screen Resolution Fix loaded (Enabled=%d) ----", g_enabled ? 1 : 0);
            if (g_enabled) InstallHook();
        }
    }
    return TRUE;
}

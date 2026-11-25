// Pass-through for the system version.dll.
// The game imports VERSION.dll; Windows loads this copy from the game folder first.
// Every export here forwards unchanged to C:\Windows\System32\version.dll.
#include <windows.h>
#include <stdint.h>

namespace {
using Generic = uintptr_t(WINAPI*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                   uintptr_t, uintptr_t, uintptr_t, uintptr_t);
HMODULE g_real = nullptr;

FARPROC Real(const char* name) {
    if (!g_real) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n > MAX_PATH - 20) return nullptr;
        lstrcatW(path, L"\\version.dll");
        g_real = LoadLibraryW(path);
        if (!g_real) return nullptr;
    }
    return GetProcAddress(g_real, name);
}
}  // namespace

// Up to 8 pointer-sized arguments covers every version.dll export (VerInstallFile has 8).
// Under the x64 calling convention, passing extra unused arguments is harmless.
#define FORWARD(name)                                                                 \
    extern "C" uintptr_t WINAPI P_##name(uintptr_t a, uintptr_t b, uintptr_t c,         \
                                         uintptr_t d, uintptr_t e, uintptr_t f,         \
                                         uintptr_t g, uintptr_t h) {                    \
        static Generic fn = reinterpret_cast<Generic>(Real(#name));                     \
        if (!fn) { SetLastError(ERROR_PROC_NOT_FOUND); return 0; }                      \
        return fn(a, b, c, d, e, f, g, h);                                              \
    }

FORWARD(GetFileVersionInfoA)
FORWARD(GetFileVersionInfoByHandle)
FORWARD(GetFileVersionInfoExA)
FORWARD(GetFileVersionInfoExW)
FORWARD(GetFileVersionInfoSizeA)
FORWARD(GetFileVersionInfoSizeExA)
FORWARD(GetFileVersionInfoSizeExW)
FORWARD(GetFileVersionInfoSizeW)
FORWARD(GetFileVersionInfoW)
FORWARD(VerFindFileA)
FORWARD(VerFindFileW)
FORWARD(VerInstallFileA)
FORWARD(VerInstallFileW)
FORWARD(VerQueryValueA)
FORWARD(VerQueryValueW)

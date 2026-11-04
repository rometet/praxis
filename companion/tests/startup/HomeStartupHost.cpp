#include "HomeStartupFixtureState.h"
#include <Windows.h>
#include <delayimp.h>
#include <cstdio>
#include <cstring>

using StateGetter = HomeStartupState const* (*)();
using Callback = unsigned (*)(unsigned);
static DWORD exceptionCode, delayError;
static char failureDll[96]{}, failureSymbol[256]{};

int delayFailure(EXCEPTION_POINTERS* details) {
    exceptionCode = details->ExceptionRecord->ExceptionCode;
    if (exceptionCode != 0xC06D007E || details->ExceptionRecord->NumberParameters != 1)
        return EXCEPTION_CONTINUE_SEARCH;
    auto* delay = reinterpret_cast<DelayLoadInfo*>(details->ExceptionRecord->ExceptionInformation[0]);
    delayError = delay->dwLastError;
    strcpy_s(failureDll, delay->szDll);
    if (delay->dlp.fImportByName) strcpy_s(failureSymbol, delay->dlp.szProcName);
    return EXCEPTION_EXECUTE_HANDLER;
}
int runCallbacks(Callback callback) {
    __try {
        callback(0); // Actual product listener returns on home, before client access.
        callback(1); // HUD has no player: actual listener returns without restock.
        callback(0); // Cached delayed import remains resolved across later frames.
        return 0;
    } __except (delayFailure(GetExceptionInformation())) {
        std::printf("CONTROLLED_DELAY_FAILURE exception=%08lX lastError=%lu dll=%s symbol=%s\n",
                    exceptionCode, delayError, failureDll, failureSymbol);
        return 126;
    }
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 2 || GetModuleHandleW(L"bedrock_runtime.dll")) return 2;
    auto module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { std::printf("fixture load failed error=%lu\n", GetLastError()); return 3; }
    auto state = reinterpret_cast<StateGetter>(GetProcAddress(module, "GetHomeStartupState"));
    auto callback = reinterpret_cast<Callback>(GetProcAddress(module, "RunHomeStartupCallback"));
    if (!state || !callback) return 4;
    auto const initial = *state();
    std::printf("AFTER_LOAD attached=%u preload=%u late=%u unknown=%u screens=%u\n",
                initial.attached, initial.preloadResolves, initial.lateResolves, initial.unknownSymbols, initial.screenCalls);
    auto result = runCallbacks(callback);
    if (result) return result;
    auto const final = *state();
    bool const pass = initial.attached == 1 && initial.preloadResolves == 1 && initial.lateResolves == 0
        && initial.unknownSymbols == 0 && initial.screenCalls == 0
        && final.preloadResolves == 1 && final.lateResolves == 0 && final.screenCalls == 3
        && final.uiQueries == 1 && final.playerQueries == 1 && final.restockCalls == 0
        && !GetModuleHandleW(L"bedrock_runtime.dll");
    std::printf("HOME_CALLBACK preload=%u late=%u screenCalls=%u uiQueries=%u playerQueries=%u restockCalls=%u runtimeDll=ABSENT %s\n",
                final.preloadResolves, final.lateResolves, final.screenCalls, final.uiQueries,
                final.playerQueries, final.restockCalls, pass ? "PASS" : "FAIL");
    FreeLibrary(module);
    return pass ? 0 : 5;
}

#include <windows.h>
#include <stdio.h>

int main(void) {
    HMODULE module = LoadLibraryW(L".\\avs-bridge.dll");
    if (!module) {
        fprintf(stderr, "FAIL: LoadLibrary error=%lu\n", GetLastError());
        return 1;
    }
    FARPROC symbol = GetProcAddress(module, "script_probe_status");
    if (!symbol) {
        fputs("FAIL: status export missing\n", stderr);
        return 1;
    }
    /* FARPROC's generic signature is intentionally converted through a union. */
    union {
        FARPROC generic;
        DWORD(WINAPI *status)(void);
    } call = {.generic = symbol};
    DWORD status = 0;
    for (unsigned tick = 0; tick < 1000 && !status; ++tick) {
        status = call.status();
        if (!status)
            Sleep(10);
    }
    if (status != 3) {
        fprintf(stderr, "FAIL: expected host rejection, got status=%lu\n", status);
        return 1;
    }
    if (GetModuleHandleW(L"avs-native-mod.dll")) {
        fputs("FAIL: wrong host loaded observation payload\n", stderr);
        return 1;
    }
    puts("PASS: resident bridge refuses non-AVS host before creating a hook or loading payload");
    /* The bridge is pinned; process exit is its cleanup boundary. */
    return 0;
}

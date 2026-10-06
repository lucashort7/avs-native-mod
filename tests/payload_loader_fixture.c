#include <windows.h>
#include "probe.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    DeleteFileW(L"payload-fixture.log");
    HMODULE module = LoadLibraryW(L".\\avs-native-mod.dll");
    if (!module)
        return 1;
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(const wchar_t *, const wchar_t *);
    } start = {.raw = GetProcAddress(module, "avs_payload_start")};
    union {
        FARPROC raw;
        void(WINAPI *fn)(const probe_sample *);
    } observe = {.raw = GetProcAddress(module, "avs_payload_observe")};
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(void);
    } flush = {.raw = GetProcAddress(module, "avs_payload_flush")};
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(void);
    } stop = {.raw = GetProcAddress(module, "avs_payload_stop")};
    if (!start.raw || !observe.raw || !flush.raw || !stop.raw) {
        fputs("FAIL: reloadable payload lifecycle exports missing\n", stderr);
        return 1;
    }
    int token;
    probe_sample sample = {.function = &token, .count = 3};
    SetLastError(0x1234);
    if (!start.fn(L".\\", L"payload-fixture"))
        return 1;
    observe.fn(&sample);
    observe.fn(&sample);
    if (!flush.fn())
        return 1;
    observe.fn(&sample);
    if (!flush.fn())
        return 1;
    if (!stop.fn() || GetLastError() != 0x1234 || !FreeLibrary(module) || GetModuleHandleW(L"avs-native-mod.dll"))
        return 1;
    FILE *file = fopen("payload-fixture.log", "rb");
    char text[4096] = {0};
    if (!file)
        return 1;
    fread(text, 1, sizeof text - 1, file);
    fclose(file);
    if (!strstr(text, "first_seen ") || !strstr(text, "interval=1 function_object=") ||
        !strstr(text, "interval=2 function_object=") || !strstr(text, "calls=3") || !strstr(text, "observed=3"))
        return 1;
    if (!strstr(text, "interval=1 function_object=") || !strstr(text, "calls_in_interval=2") ||
        !strstr(text, "interval=2 function_object=") || !strstr(text, "calls_in_interval=1"))
        return 1;
    puts("PASS: actual payload observes, flushes and unmaps without threads or pinning");
    return 0;
}

#include "payload.h"
#include "logger.h"
#include "MinHook.h"
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <strsafe.h>

static HMODULE g_module, g_payload;
static wchar_t g_directory[MAX_PATH], g_payload_path[MAX_PATH];
static wchar_t g_enable_path[MAX_PATH], g_unload_path[MAX_PATH], g_stop_path[MAX_PATH];
static SRWLOCK g_gate = SRWLOCK_INIT;
static volatile LONG g_admission, g_status;
static payload_observe_fn g_observe;
static payload_stop_fn g_stop;
static payload_flush_fn g_flush;
static script_call_fn g_original;
static void *g_target;
static BOOL g_hook_enabled, g_faulted;
static HANDLE g_request, g_done, g_ready;

static DWORD g_command;
static wchar_t g_requested_name[MAX_PATH];
static BOOL g_result;
#ifdef AVS_BRIDGE_TESTING
static SRWLOCK g_request_lock = SRWLOCK_INIT;
static unsigned char g_expected[64];
static size_t g_expected_size;
static HANDLE g_before_entered, g_before_release, g_admission_closed;
static volatile LONG g_pause_before;
#endif

__declspec(dllexport) DWORD WINAPI script_probe_status(void) {
    DWORD saved = GetLastError();
    DWORD status = (DWORD)InterlockedCompareExchange(&g_status, 0, 0);
    SetLastError(saved);
    return status;
}
static void status(LONG value) { InterlockedExchange(&g_status, value); }
/* Only a regular, newly created file is a command. Errors are not absence. */
static int flag_state(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ? -1 : 1;
    return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
}
static BOOL consume_flag(const wchar_t *path) { return DeleteFileW(path) && flag_state(path) == 0; }

/* Return address, gate and trampoline all belong to permanently resident code. */
static void *observe_call(void *result, void *function, void *instance, const void *const *args, int count, void *error,
                          void *state) {
    DWORD saved = GetLastError();
    if (InterlockedCompareExchange(&g_admission, 0, 0)) {
#ifdef AVS_BRIDGE_TESTING
        if (InterlockedExchange(&g_pause_before, 0)) {
            SetEvent(g_before_entered);
            WaitForSingleObject(g_before_release, INFINITE);
        }
#endif
        AcquireSRWLockShared(&g_gate);
        if (InterlockedCompareExchange(&g_admission, 0, 0) && g_observe) {
            probe_sample sample = {result, function, instance, args, count, error, state};
            g_observe(&sample);
        }
        ReleaseSRWLockShared(&g_gate);
    }
    SetLastError(saved);
    /* Never hold the nonrecursive SRW gate across the original. */
    return g_original(result, function, instance, args, count, error, state);
}

static BOOL module_absent(HMODULE module, const wchar_t *path) {
    MEMORY_BASIC_INFORMATION region;
    if (GetModuleHandleW(path))
        return FALSE;
    return VirtualQuery((const void *)module, &region, sizeof region) == sizeof region &&
           !(region.Type == MEM_IMAGE && region.AllocationBase == (void *)module);
}

static BOOL unload_payload(void) {
    /* Stop new readers before waiting for the exclusive gate. */
    InterlockedExchange(&g_admission, 0);
#ifdef AVS_BRIDGE_TESTING
    if (g_admission_closed)
        SetEvent(g_admission_closed);
#endif
    AcquireSRWLockExclusive(&g_gate);
    BOOL ok = !g_faulted;
    if (g_payload) {
        ok = ok && g_stop && g_stop();
        g_observe = NULL;
        g_stop = NULL;
        g_flush = NULL;
        if (ok) {
            HMODULE previous = g_payload;
            ok = FreeLibrary(previous) && module_absent(previous, g_payload_path);
            if (ok)
                g_payload = NULL;
        }
        /* Cleanup failure or extra references permanently forbid another load. */
        if (!ok)
            g_faulted = TRUE;
    }
    ReleaseSRWLockExclusive(&g_gate);
    wlog("payload stop: success=%d unmapped=%d reload_allowed=%d", ok, g_payload == NULL, !g_faulted);
    logger_flush();
    status(ok ? 2 : 4);
    return ok;
}

static BOOL load_payload(const wchar_t *name) {
    if (g_faulted || g_payload || !name || !*name || wcschr(name, L'\\') || wcschr(name, L'/') || wcschr(name, L':') ||
        wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
        return FALSE;
    if (FAILED(StringCchPrintfW(g_payload_path, ARRAYSIZE(g_payload_path), L"%ls%ls", g_directory, name)))
        return FALSE;
    /* An externally held module is not a fresh capture and is never adopted. */
    if (GetModuleHandleW(g_payload_path))
        return FALSE;
    HMODULE module = LoadLibraryExW(g_payload_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    DWORD load_error = module ? ERROR_SUCCESS : GetLastError();
    if (!module) {
        wlog("payload load refused: error=%lu", (unsigned long)load_error);
        logger_flush();
        status(8);
        return FALSE;
    }
    union {
        FARPROC raw;
        payload_start_fn fn;
    } start = {.raw = GetProcAddress(module, "avs_payload_start")};
    union {
        FARPROC raw;
        payload_observe_fn fn;
    } observe = {.raw = GetProcAddress(module, "avs_payload_observe")};
    union {
        FARPROC raw;
        payload_stop_fn fn;
    } stop = {.raw = GetProcAddress(module, "avs_payload_stop")};
    union {
        FARPROC raw;
        payload_flush_fn fn;
    } flush = {.raw = GetProcAddress(module, "avs_payload_flush")};
    if (!start.raw || !observe.raw || !stop.raw || !flush.raw) {
        BOOL released = FreeLibrary(module) && module_absent(module, g_payload_path);
        g_faulted = !released;
        status(released ? 8 : 4);
        return FALSE;
    }
    /* From start onward, all cleanup is checked, including partial initialization. */
    AcquireSRWLockExclusive(&g_gate);
    g_payload = module;
    g_stop = stop.fn;
    g_flush = flush.fn;
#ifdef AVS_BRIDGE_TESTING
    BOOL started = start.fn(g_directory, L"avs-native-mod-fixture");
#else
    BOOL started = start.fn(g_directory, L"avs-native-mod");
#endif
    if (started)
        g_observe = observe.fn;
    ReleaseSRWLockExclusive(&g_gate);
    if (!started) {
        unload_payload();
        return FALSE;
    }
    if (!g_hook_enabled) {
        /* An uncertain activation leaves bridge and trampoline resident too. */
        if (MH_EnableHook(g_target) != MH_OK) {
            unload_payload();
            g_faulted = TRUE;
            status(4);
            return FALSE;
        }
        g_hook_enabled = TRUE;
    }
    InterlockedExchange(&g_admission, 1);
    wlog("payload loaded from absolute sibling path; observation admission open");
    logger_flush();
    status(7);
    return TRUE;
}

#ifndef AVS_BRIDGE_TESTING
#define TARGET_RVA 0x58c370u
#define TARGET_TIMESTAMP 0x6a555660u
#define TARGET_IMAGE_SIZE 0x69a1000u
static const unsigned char target_entry[] = {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x57, 0x56,
                                             0x53, 0x48, 0x81, 0xec, 0x58, 0x02, 0x00, 0x00, 0x48, 0x8d, 0xac,
                                             0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89, 0x8d, 0x20, 0x02};
static BOOL identify_host(void) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    if (!n || n >= ARRAYSIZE(path))
        return FALSE;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || _wcsicmp(slash + 1, L"AVS03Pro.exe")) {
        wlog("host mismatch; no hook; no payload");
        status(3);
        return FALSE;
    }
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return FALSE;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->FileHeader.TimeDateStamp != TARGET_TIMESTAMP ||
        nt->OptionalHeader.SizeOfImage != TARGET_IMAGE_SIZE)
        return FALSE;
    g_target = base + TARGET_RVA;
    return TRUE;
}
#endif

static BOOL prepare_bridge(void) {
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)prepare_bridge, &pinned))
        return FALSE;
    DWORD n = GetModuleFileNameW(g_module, g_directory, ARRAYSIZE(g_directory));
    if (!n || n >= ARRAYSIZE(g_directory))
        return FALSE;
    wchar_t *slash = wcsrchr(g_directory, L'\\');
    if (!slash)
        return FALSE;
    slash[1] = L'\0';
#ifdef AVS_BRIDGE_TESTING
    if (!logger_init(g_directory, L"avs-bridge-fixture"))
#else
    if (!logger_init(g_directory, L"avs-bridge"))
#endif
        return FALSE;
    if (FAILED(StringCchPrintfW(g_enable_path, ARRAYSIZE(g_enable_path), L"%lsavs-native-mod.enable", g_directory)) ||
        FAILED(StringCchPrintfW(g_unload_path, ARRAYSIZE(g_unload_path), L"%lsavs-native-mod.unload", g_directory)) ||
        FAILED(StringCchPrintfW(g_stop_path, ARRAYSIZE(g_stop_path), L"%lsavs-native-mod.stop", g_directory)) ||
        flag_state(g_enable_path) != 0 || flag_state(g_unload_path) != 0 || flag_state(g_stop_path) != 0)
        return FALSE;
    const unsigned char *expected;
    size_t size;
#ifdef AVS_BRIDGE_TESTING
    expected = g_expected;
    size = g_expected_size;
#else
    if (!identify_host())
        return FALSE;
    expected = target_entry;
    size = sizeof target_entry;
#endif
    unsigned char actual[64];
    SIZE_T read = 0;
    if (!g_target || size < 16 || size > sizeof actual ||
        !ReadProcessMemory(GetCurrentProcess(), g_target, actual, size, &read) || read != size ||
        memcmp(actual, expected, size) || MH_Initialize() != MH_OK)
        return FALSE;
    LPVOID trampoline = NULL;
    if (MH_CreateHook(g_target, (LPVOID)observe_call, &trampoline) != MH_OK)
        return FALSE;
    g_original = (script_call_fn)trampoline;
    g_request = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_request || !g_done)
        return FALSE;
    if (!wlog("prepared DISABLED target=%p; bridge pinned; payload absent; waiting for .enable", g_target) ||
        !logger_flush())
        return FALSE;
    status(1);
    return TRUE;
}
static BOOL poll_controls(void) {
    if (g_faulted)
        return TRUE;
    int enable = flag_state(g_enable_path);
    int unload = flag_state(g_unload_path);
    int stop = flag_state(g_stop_path);
    if (enable < 0 || unload < 0 || stop < 0 || enable + unload + stop > 1 || (enable && g_payload))
        return FALSE;
    if (unload || stop) {
        if (!consume_flag(unload ? g_unload_path : g_stop_path))
            return FALSE;
        unload_payload();
    } else if (enable) {
        if (!consume_flag(g_enable_path))
            return FALSE;
        /* Missing/invalid payload is recoverable only after verified release. */
        load_payload(L"avs-native-mod.dll");
    }
    return TRUE;
}
static DWORD WINAPI controller(LPVOID context) {
    (void)context;
    if (!prepare_bridge()) {
        if (script_probe_status() != 3)
            status(4);
        wlog("initialization refused; status=%lu; no activation", (unsigned long)script_probe_status());
        logger_close();
        if (g_ready)
            SetEvent(g_ready);
        return 1;
    }
    if (g_ready)
        SetEvent(g_ready);
    ULONGLONG last_flush = GetTickCount64();
    for (;;) {
        DWORD wait = WaitForSingleObject(g_request, 50);
        if (wait == WAIT_OBJECT_0) {
            g_result = g_command == 1 ? load_payload(g_requested_name) : unload_payload();
            SetEvent(g_done);
        } else if (wait != WAIT_TIMEOUT) {
            unload_payload();
            status(4);
            return 1;
        }
        BOOL healthy = poll_controls();
        if (healthy && GetTickCount64() - last_flush >= 2000) {
            AcquireSRWLockShared(&g_gate);
            healthy = !g_flush || g_flush();
            ReleaseSRWLockShared(&g_gate);
            last_flush = GetTickCount64();
        }
        if (!healthy) {
            unload_payload();
            g_faulted = TRUE;
            status(4);
            wlog("control/flush error; admission closed; restart required");
            logger_flush();
        }
    }
}

#ifdef AVS_BRIDGE_TESTING
__declspec(dllexport) BOOL WINAPI avs_bridge_fixture_prepare(void *target, const unsigned char *expected, size_t size) {
    if (g_ready || !target || !expected || size > sizeof g_expected)
        return FALSE;
    g_target = target;
    memcpy(g_expected, expected, size);
    g_expected_size = size;
    g_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_ready)
        return FALSE;
    HANDLE thread = CreateThread(NULL, 0, controller, NULL, 0, NULL);
    if (!thread)
        return FALSE;
    CloseHandle(thread);
    return WaitForSingleObject(g_ready, 10000) == WAIT_OBJECT_0 && script_probe_status() == 1;
}
/* Fixture changes these only when no target calls are in flight. */
__declspec(dllexport) void WINAPI avs_bridge_fixture_barriers(HANDLE before, HANDLE resume, HANDLE closed) {
    g_before_entered = before;
    g_before_release = resume;
    g_admission_closed = closed;
    InterlockedExchange(&g_pause_before, before && resume);
}
__declspec(dllexport) BOOL WINAPI avs_bridge_fixture_command(DWORD command, const wchar_t *name) {
    DWORD saved = GetLastError();
    AcquireSRWLockExclusive(&g_request_lock);
    BOOL ok = g_request && (command == 1 || command == 2);
    if (ok && command == 1)
        ok = name && SUCCEEDED(StringCchCopyW(g_requested_name, ARRAYSIZE(g_requested_name), name));
    if (ok) {
        g_command = command;
        ok = SetEvent(g_request) && WaitForSingleObject(g_done, INFINITE) == WAIT_OBJECT_0 && g_result;
    }
    ReleaseSRWLockExclusive(&g_request_lock);
    SetLastError(saved);
    return ok;
}
#endif
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
#ifndef AVS_BRIDGE_TESTING
        HANDLE thread = CreateThread(NULL, 0, controller, NULL, 0, NULL);
        if (!thread)
            return FALSE;
        CloseHandle(thread);
#endif
    }
    return TRUE;
}

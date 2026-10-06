#include "probe.h"
#include "logger.h"
#include <stdint.h>
#include <strsafe.h>
#include <wchar.h>

static HMODULE g_module;
static volatile LONG g_status;

/* Measured from the installed executable, not copied from engine source. */
#define TARGET_RVA 0x58c370u
#define TARGET_TIMESTAMP 0x6a555660u
#define TARGET_IMAGE_SIZE 0x69a1000u
static const unsigned char target_entry[] = {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x57, 0x56,
                                             0x53, 0x48, 0x81, 0xec, 0x58, 0x02, 0x00, 0x00, 0x48, 0x8d, 0xac,
                                             0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89, 0x8d, 0x20, 0x02};

__declspec(dllexport) DWORD WINAPI script_probe_status(void) {
    return (DWORD)InterlockedCompareExchange(&g_status, 0, 0);
}

static DWORD finish(LONG status) {
    if (!logger_close())
        status = 4;
    InterlockedExchange(&g_status, status);
    return (DWORD)status;
}

/* 0 = absent, 1 = regular file, -1 = unsafe or inaccessible. */
static int flag_state(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? -1 : 1;
    DWORD error = GetLastError();
    return (error == ERROR_FILE_NOT_FOUND) ? 0 : -1;
}

static BOOL consume_flag(const wchar_t *path) { return DeleteFileW(path) && flag_state(path) == 0; }

static DWORD WINAPI worker(LPVOID context) {
    (void)context;
    /* Pin before any hook resources exist: generic ejectors cannot free our code. */
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)worker, &pinned))
        return finish(4);
    static wchar_t directory[MAX_PATH];
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_module, directory, ARRAYSIZE(directory));
    if (!n || n >= ARRAYSIZE(directory))
        return finish(4);
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (!slash)
        return finish(4);
    slash[1] = L'\0';
    if (!logger_init(directory, L"avs-native-mod"))
        return finish(4);
    if (!wlog("attached pid=%lu; DLL pinned until process exit; candidate ABI not yet runtime-confirmed",
              (unsigned long)GetCurrentProcessId()) ||
        !logger_flush())
        return finish(4);

    n = GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    if (!n || n >= ARRAYSIZE(path))
        return finish(4);
    slash = wcsrchr(path, L'\\');
    if (!slash || _wcsicmp(slash + 1, L"AVS03Pro.exe") != 0) {
        wlog("host mismatch; no hook");
        return finish(3);
    }
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    if (!base)
        return finish(4);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return finish(4);
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->FileHeader.TimeDateStamp != TARGET_TIMESTAMP ||
        nt->OptionalHeader.SizeOfImage != TARGET_IMAGE_SIZE) {
        wlog("build mismatch; no hook");
        return finish(4);
    }
    wchar_t enable_path[MAX_PATH], stop_path[MAX_PATH];
    if (FAILED(StringCchPrintfW(enable_path, ARRAYSIZE(enable_path), L"%lsavs-native-mod.enable", directory)) ||
        FAILED(StringCchPrintfW(stop_path, ARRAYSIZE(stop_path), L"%lsavs-native-mod.stop", directory)))
        return finish(4);
    if (flag_state(enable_path) != 0 || flag_state(stop_path) != 0) {
        wlog("stale or inaccessible control flag; refusing to prepare");
        return finish(4);
    }
    if (!probe_prepare(base + TARGET_RVA, target_entry, sizeof target_entry))
        return finish(4);
    if (!wlog("prepared DISABLED target=%p rva=0x%x; waiting up to 300 seconds for .enable or .stop", base + TARGET_RVA,
              TARGET_RVA) ||
        !logger_flush()) {
        probe_release_disabled();
        return finish(4);
    }
    InterlockedExchange(&g_status, 1);
    BOOL requested = FALSE;
    /* A bounded native watcher; no CLI subprocess polling. */
    ULONGLONG waiting_since = GetTickCount64();
    while (GetTickCount64() - waiting_since < 300000) {
        int stop = flag_state(stop_path);
        int enable = flag_state(enable_path);
        if (stop < 0 || enable < 0) {
            probe_release_disabled();
            return finish(4);
        }
        if (stop) {
            BOOL consumed = consume_flag(stop_path);
            BOOL released = probe_release_disabled();
            BOOL ok = consumed && released;
            wlog("cancelled before activation; resources released=%d; DLL remains pinned", released);
            return finish(ok ? 6 : 4);
        }
        if (enable) {
            requested = consume_flag(enable_path);
            break;
        }
        Sleep(250);
    }
    if (!requested) {
        BOOL ok = probe_release_disabled();
        wlog("no activation request consumed; disabled resources released=%d", ok);
        return finish(ok ? 5 : 4);
    }
    BOOL enabled = probe_enable();
    BOOL healthy =
        enabled &&
        wlog("continuous inventory active: first-seen function keys; repeat counters; flush_ms=2000; stop via .stop") &&
        logger_flush();
    if (healthy) {
        InterlockedExchange(&g_status, 7);
        healthy = probe_run_capture(stop_path, 2000);
    }
    BOOL stopped = probe_stop();
    /* Freeze metadata separately: disable does not cancel in-flight originals. */
    BOOL drained = probe_write_inventory(TRUE);
    BOOL reported = wlog("capture end: stopped=%d inventory_flushed=%d; DLL and trampoline RETAINED; do not eject",
                         stopped, drained);
    return finish(healthy && stopped && drained && reported ? 2 : 4);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (!thread)
            return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}

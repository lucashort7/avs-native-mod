#include "probe.h"
/* Historical retention-only implementation. Production uses bridge.c instead.
 * This object is linked solely into hook_fixture for baseline regressions. */
#include "logger.h"
#include "inventory.h"
#include "MinHook.h"
#include <string.h>

static void *g_target;
static script_call_fn g_original;
static BOOL g_initialized;
static BOOL g_created;
static BOOL g_activation_attempted;
static volatile LONG64 g_calls __attribute__((aligned(8)));
static probe_sample g_samples[PROBE_SAMPLE_CAPACITY];
static volatile LONG g_ready[PROBE_SAMPLE_CAPACITY];
static BOOL g_logged[INVENTORY_CAPACITY];
static BOOL g_inventory_finalized;

static void *observe_call(void *result, void *function, void *instance, const void *const *args, int count, void *error,
                          void *state) {
    DWORD last_error = GetLastError();
    LONG64 ticket = InterlockedIncrement64(&g_calls);
    probe_sample observation = {result, function, instance, args, count, error, state};
    inventory_observe(&observation);
    /* No file I/O, allocation or object dereference inside the detour.
     * Each slot has one writer and is published only after it is complete. */
    if (ticket > 0 && ticket <= PROBE_SAMPLE_CAPACITY) {
        unsigned index = (unsigned)(ticket - 1);
        g_samples[index] = (probe_sample){result, function, instance, args, count, error, state};
        InterlockedExchange(&g_ready[index], 1);
    }
    SetLastError(last_error);
    return g_original(result, function, instance, args, count, error, state);
}

static BOOL status_ok(const char *stage, MH_STATUS status) {
    wlog("script probe: %s => %s", stage, MH_StatusToString(status));
    return status == MH_OK;
}

BOOL probe_prepare(void *target, const unsigned char *expected, size_t size) {
    unsigned char actual[64];
    SIZE_T read = 0;
    if (g_initialized || !target || !expected || size < 16 || size > sizeof actual)
        return FALSE;
    if (!ReadProcessMemory(GetCurrentProcess(), target, actual, size, &read) || read != size ||
        memcmp(actual, expected, size) != 0) {
        wlog("script probe: entry mismatch; no hook created");
        return FALSE;
    }
    if (!status_ok("initialize", MH_Initialize()))
        return FALSE;
    g_initialized = TRUE;
    g_target = target;
    LPVOID trampoline = NULL;
    if (!status_ok("create disabled", MH_CreateHook(target, (LPVOID)observe_call, &trampoline))) {
        probe_release_disabled();
        return FALSE;
    }
    g_original = (script_call_fn)trampoline;
    g_created = TRUE;
    return TRUE;
}

BOOL probe_enable(void) {
    if (!g_created || g_activation_attempted)
        return FALSE;
    /* Even a failed activation can leave execution uncertain: retain resources. */
    g_activation_attempted = TRUE;
    return status_ok("enable", MH_EnableHook(g_target));
}

BOOL probe_stop(void) {
    if (!g_created)
        return TRUE;
    MH_STATUS status = MH_DisableHook(g_target);
    wlog("script probe: stop => %s", MH_StatusToString(status));
    return status == MH_OK || status == MH_ERROR_DISABLED;
}

BOOL probe_release_disabled(void) {
    if (g_activation_attempted)
        return FALSE;
    if (g_created) {
        if (!status_ok("remove never-enabled", MH_RemoveHook(g_target)))
            return FALSE;
        g_created = FALSE;
    }
    if (g_initialized) {
        if (!status_ok("uninitialize never-enabled", MH_Uninitialize()))
            return FALSE;
        g_initialized = FALSE;
    }
    g_target = NULL;
    g_original = NULL;
    return TRUE;
}

BOOL probe_retained(void) { return g_activation_attempted; }
BOOL probe_run_capture(const wchar_t *stop_path, DWORD flush_ms) {
    if (!stop_path || !flush_ms)
        return FALSE;
    ULONGLONG last_flush = GetTickCount64();
    for (;;) {
        DWORD attrs = GetFileAttributesW(stop_path);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) || !DeleteFileW(stop_path))
                return FALSE;
            return GetFileAttributesW(stop_path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            return FALSE;
        if (GetTickCount64() - last_flush >= flush_ms) {
            if (!probe_write_inventory(FALSE))
                return FALSE;
            last_flush = GetTickCount64();
        }
        /* Intentionally continuous native watcher; not a CLI polling loop. */
        Sleep(50);
    }
}
BOOL probe_write_inventory(BOOL final) {
    if (g_inventory_finalized)
        return FALSE;
    if (final) {
        inventory_close();
        g_inventory_finalized = TRUE;
    }
    for (unsigned i = 0; i < INVENTORY_CAPACITY; ++i) {
        inventory_entry entry;
        if (!inventory_get(i, &entry))
            continue;
        probe_sample *sample = &entry.first;
        if (!g_logged[i]) {
            if (!wlog("first_seen function_object=%p instance=%p argc=%d state=%p result=%p args=%p error=%p",
                      sample->function, sample->instance, sample->count, sample->state, sample->result,
                      (const void *)sample->args, sample->error))
                return FALSE;
            g_logged[i] = TRUE;
        }
        if (final &&
            !wlog("function_total function_object=%p calls=%llu", sample->function, (unsigned long long)entry.calls))
            return FALSE;
    }
    if (final) {
        inventory_stats stats = inventory_totals();
        if (!wlog("inventory end: observed=%llu unique=%ld untracked_calls=%llu; pointer identity is capture-scoped",
                  (unsigned long long)stats.observed, stats.unique, (unsigned long long)stats.untracked))
            return FALSE;
    }
    return logger_flush();
}
LONG64 probe_call_count(void) { return InterlockedCompareExchange64(&g_calls, 0, 0); }

BOOL probe_get_sample(unsigned index, probe_sample *sample) {
    if (!sample || index >= PROBE_SAMPLE_CAPACITY || !InterlockedCompareExchange(&g_ready[index], 0, 0))
        return FALSE;
    *sample = g_samples[index];
    return TRUE;
}

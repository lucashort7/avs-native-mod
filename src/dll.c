#include "payload.h"
#include "inventory.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

static BOOL g_logged[INVENTORY_CAPACITY];
static LONG64 g_reported[INVENTORY_CAPACITY];
static unsigned long long g_interval;
static BOOL g_failed;
static BOOL g_started;
static BOOL g_argument_logged[ARG_SAMPLE_CAPACITY];
static BOOL g_override_logged;

static void format_argument(unsigned index, const argument_value *value, char *text, size_t capacity) {
    if (!value->readable) {
        snprintf(text, capacity, "arg%u_type=<unreadable> arg%u_data=<unreadable> arg%u_value=<unreadable>", index,
                 index, index);
    } else if (value->type == 1) {
        snprintf(text, capacity, "arg%u_type=1 arg%u_data=%016llx arg%u_value=%s", index, index,
                 (unsigned long long)value->data, index, value->data ? "true" : "false");
    } else if (value->type == 2) {
        snprintf(text, capacity, "arg%u_type=2 arg%u_data=%016llx arg%u_value=%lld", index, index,
                 (unsigned long long)value->data, index, (long long)value->data);
    } else if (value->type == 3) {
        double number;
        memcpy(&number, &value->data, sizeof number);
        snprintf(text, capacity, "arg%u_type=3 arg%u_data=%016llx arg%u_value=%.17g", index, index,
                 (unsigned long long)value->data, index, number);
    } else {
        snprintf(text, capacity, "arg%u_type=%u arg%u_data=%016llx arg%u_value=<unsupported>", index, value->type,
                 index, (unsigned long long)value->data, index);
    }
}
#ifndef AVS_PAYLOAD_GENERATION
#define AVS_PAYLOAD_GENERATION 1
#endif
#ifdef AVS_PAYLOAD_TESTING
static HANDLE g_entered, g_release;
static volatile LONG g_pause;
static BOOL WINAPI zero_write(HANDLE file, LPCVOID text, DWORD size, LPDWORD written, LPOVERLAPPED overlap) {
    (void)file;
    (void)text;
    (void)size;
    (void)overlap;
    *written = 0;
    return TRUE;
}
__declspec(dllexport) void WINAPI avs_payload_fixture_fail_flush(void) { logger_test_set_writer(zero_write); }
__declspec(dllexport) void WINAPI avs_payload_fixture_pause(HANDLE entered, HANDLE release) {
    g_entered = entered;
    g_release = release;
    InterlockedExchange(&g_pause, 1);
}
#endif

__declspec(dllexport) BOOL WINAPI avs_payload_start(const wchar_t *directory, const wchar_t *name) {
    DWORD saved = GetLastError();
    BOOL ok = !g_started && logger_init(directory, name);
    if (ok) {
        g_started = TRUE;
        ok = wlog("capture start: generation=%d", AVS_PAYLOAD_GENERATION);
    }
    SetLastError(saved);
    return ok;
}

/* The one-shot Coin probe may alter an argument before the bridge forwards the
 * original call. Never call the target, original, bridge or another observer. */
__declspec(dllexport) void WINAPI avs_payload_observe(const probe_sample *sample) {
    DWORD saved = GetLastError();
#ifdef AVS_PAYLOAD_TESTING
    if (InterlockedExchange(&g_pause, 0)) {
        SetEvent(g_entered);
        WaitForSingleObject(g_release, INFINITE);
    }
#endif
    inventory_observe(sample);
    SetLastError(saved);
}

__declspec(dllexport) BOOL WINAPI avs_payload_flush(void) {
    DWORD saved = GetLastError();
    BOOL ok = g_started && !g_failed;
    if (ok)
        ++g_interval;
    for (unsigned i = 0; ok && i < INVENTORY_CAPACITY; ++i) {
        inventory_entry entry;
        if (g_logged[i] || !inventory_get(i, &entry))
            continue;
        probe_sample *s = &entry.first;
        ok = wlog("first_seen function_object=%p instance=%p argc=%d state=%p result=%p args=%p error=%p name=%s "
                  "source=%s",
                  s->function, s->instance, s->count, s->state, s->result, (const void *)s->args, s->error,
                  entry.label.name, entry.label.source);
        if (ok)
            g_logged[i] = TRUE;
    }
    for (unsigned i = 0; ok && i < INVENTORY_CAPACITY; ++i) {
        inventory_entry entry;
        if (!inventory_get(i, &entry) || entry.calls <= g_reported[i])
            continue;
        LONG64 delta = entry.calls - g_reported[i];
        ok = wlog("function_delta interval=%llu function_object=%p calls_in_interval=%llu", g_interval,
                  entry.first.function, (unsigned long long)delta);
        if (ok)
            g_reported[i] = entry.calls;
    }
    for (unsigned i = 0; ok && i < ARG_SAMPLE_CAPACITY; ++i) {
        argument_sample snapshot;
        if (g_argument_logged[i] || !inventory_argument_get(i, &snapshot))
            continue;
        char first[96], second[96];
        format_argument(0, &snapshot.value[0], first, sizeof first);
        format_argument(1, &snapshot.value[1], second, sizeof second);
        ok = wlog("arg_sample seq=%u function_object=%p %s %s", i + 1, snapshot.function, first, second);
        if (ok)
            g_argument_logged[i] = TRUE;
    }
    coin_override override;
    if (ok && !g_override_logged && inventory_coin_override_get(&override)) {
        ok = wlog("coin_x10 target=game_events.emit_currency_collected function_object=%p original=%.17g "
                  "multiplied=%.17g success=%d",
                  override.function, override.original, override.multiplied, override.success);
        if (ok)
            g_override_logged = TRUE;
    }
    if (ok)
        ok = logger_flush();
    if (!ok)
        g_failed = TRUE;
    SetLastError(saved);
    return ok;
}

__declspec(dllexport) BOOL WINAPI avs_payload_stop(void) {
    DWORD saved = GetLastError();
    inventory_close();
    BOOL ok = avs_payload_flush();
    for (unsigned i = 0; ok && i < INVENTORY_CAPACITY; ++i) {
        inventory_entry entry;
        if (inventory_get(i, &entry))
            ok = wlog("function_total function_object=%p calls=%llu", entry.first.function,
                      (unsigned long long)entry.calls);
    }
    inventory_stats stats = inventory_totals();
    if (ok)
        ok = wlog("inventory end: observed=%llu unique=%ld untracked_calls=%llu; pointer identity is capture-scoped",
                  (unsigned long long)stats.observed, stats.unique, (unsigned long long)stats.untracked);
    BOOL closed = logger_close();
    SetLastError(saved);
    return ok && closed;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    /* No threads, locks, pinning, hooks, callbacks or I/O under loader lock. */
    return TRUE;
}
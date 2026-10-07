#include "inventory.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct inventory_slot {
    PVOID volatile key;
    volatile LONG ready;
    volatile LONG64 calls __attribute__((aligned(8)));
    probe_sample first;
    function_label label;
    BOOL currency_ui;
    BOOL currency_emitter;
} inventory_slot;

static inventory_slot g_slots[INVENTORY_CAPACITY];
static SRWLOCK g_gate = SRWLOCK_INIT;
static BOOL g_accepting = TRUE;
static volatile LONG64 g_observed __attribute__((aligned(8)));
static volatile LONG64 g_untracked __attribute__((aligned(8)));
static volatile LONG g_unique;
static argument_sample g_arguments[ARG_SAMPLE_CAPACITY];
static volatile LONG g_argument_ready[ARG_SAMPLE_CAPACITY];
static volatile LONG g_argument_count;
static coin_override g_override;
static volatile LONG g_override_attempted;
static volatile LONG g_override_ready;

static BOOL read_exact(const void *address, void *buffer, SIZE_T size) {
    SIZE_T received = 0;
    return (uintptr_t)address >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), address, buffer, size, &received) &&
           received == size;
}

static void double_first_coin(const probe_sample *call) {
    if ((call->count != 2 && call->count != 4) || !call->args ||
        InterlockedCompareExchange(&g_override_attempted, 0, 0))
        return;
    const void *pointers[4] = {0};
    unsigned char values[4][16];
    SIZE_T pointer_bytes = (SIZE_T)call->count * sizeof pointers[0];
    if (!read_exact(call->args, pointers, pointer_bytes))
        return;
    for (int i = 0; i < call->count; ++i) {
        if (!read_exact(pointers[i], values[i], sizeof values[i]))
            return;
    }
    uint32_t types[4];
    uint64_t payloads[4];
    for (int i = 0; i < call->count; ++i) {
        memcpy(&types[i], values[i], sizeof types[i]);
        memcpy(&payloads[i], values[i] + 8, sizeof payloads[i]);
    }
    if (types[0] != 4 || types[1] != 3)
        return;
    if (call->count == 4 && (types[2] != 1 || types[3] != 1 || payloads[2] > 1 || payloads[3] > 1))
        return;
    const uint32_t coin[5] = {'C', 'o', 'i', 'n', 0};
    uint32_t name[5] = {0};
    if (!read_exact((const void *)(uintptr_t)payloads[0], name, sizeof name) || memcmp(name, coin, sizeof coin))
        return;
    double amount = 0;
    memcpy(&amount, &payloads[1], sizeof amount);
    if (!isfinite(amount) || amount <= 0 || amount > 1000 || InterlockedCompareExchange(&g_override_attempted, 1, 0))
        return;
    double multiplied = amount * 10;
    SIZE_T written = 0;
    void *destination = (unsigned char *)pointers[1] + 8;
    BOOL success = WriteProcessMemory(GetCurrentProcess(), destination, &multiplied, sizeof multiplied, &written) &&
                   written == sizeof multiplied;
    g_override = (coin_override){call->function, amount, multiplied, success};
    InterlockedExchange(&g_override_ready, 1);
}

static void capture_arguments(const probe_sample *call) {
    LONG next = InterlockedIncrement(&g_argument_count);
    if (next > (LONG)ARG_SAMPLE_CAPACITY)
        return;
    argument_sample *snapshot = &g_arguments[next - 1];
    snapshot->function = call->function;
    if (call->count == 2 && call->args) {
        const void *pointers[2] = {0};
        SIZE_T received = 0;
        if (ReadProcessMemory(GetCurrentProcess(), call->args, pointers, sizeof pointers, &received) &&
            received == sizeof pointers) {
            for (unsigned i = 0; i < 2; ++i) {
                /* Candidate Variant layout: 32-bit type at +0, 64-bit scalar at +8.
                 * Never follow nested pointers. */
                unsigned char bytes[16];
                if ((uintptr_t)pointers[i] >= 0x10000 &&
                    ReadProcessMemory(GetCurrentProcess(), pointers[i], bytes, sizeof bytes, &received) &&
                    received == sizeof bytes) {
                    memcpy(&snapshot->value[i].type, bytes, sizeof(uint32_t));
                    memcpy(&snapshot->value[i].data, bytes + 8, sizeof(uint64_t));
                    snapshot->value[i].readable = TRUE;
                }
            }
        }
    }
    InterlockedExchange(&g_argument_ready[next - 1], 1);
}

void inventory_observe(const probe_sample *sample) {
    if (!sample)
        return;
    DWORD saved_error = GetLastError();
    /* Shared for concurrent producers; close takes exclusive only long enough
     * to freeze admission. No allocation, file access or original call here. */
    AcquireSRWLockShared(&g_gate);
    if (!g_accepting)
        goto done;
    InterlockedIncrement64(&g_observed);
    if (sample->function) {
        uintptr_t key = (uintptr_t)sample->function;
        unsigned start = (unsigned)((key >> 3) ^ (key >> 17)) % INVENTORY_CAPACITY;
        unsigned limit = INVENTORY_CAPACITY < 64u ? INVENTORY_CAPACITY : 64u;
        for (unsigned probe = 0; probe < limit; ++probe) {
            inventory_slot *slot = &g_slots[(start + probe) % INVENTORY_CAPACITY];
            PVOID prior = InterlockedCompareExchangePointer(&slot->key, sample->function, NULL);
            if (!prior || prior == sample->function) {
                InterlockedIncrement64(&slot->calls);
                if (!prior) {
                    slot->first = *sample;
                    function_name_read(sample->function, &slot->label);
                    slot->currency_ui = strcmp(slot->label.name, "on_currency_collected") == 0 &&
                                        strcmp(slot->label.source, "res://scenes/ui/game_currency_ui.gd") == 0;
                    slot->currency_emitter = strcmp(slot->label.name, "emit_currency_collected") == 0 &&
                                             strcmp(slot->label.source, "res://scenes/autoload/game_events.gd") == 0;
                    InterlockedIncrement(&g_unique);
                    InterlockedExchange(&slot->ready, 1);
                }
                if (slot->currency_ui)
                    capture_arguments(sample);
                if (slot->currency_emitter)
                    double_first_coin(sample);
                goto done;
            }
        }
    }
    /* Includes null keys, a full table or the bounded collision-probe budget. */
    InterlockedIncrement64(&g_untracked);
done:
    ReleaseSRWLockShared(&g_gate);
    SetLastError(saved_error);
}

BOOL inventory_get(unsigned index, inventory_entry *entry) {
    if (!entry || index >= INVENTORY_CAPACITY || !InterlockedCompareExchange(&g_slots[index].ready, 0, 0))
        return FALSE;
    entry->first = g_slots[index].first;
    entry->label = g_slots[index].label;
    entry->calls = InterlockedCompareExchange64(&g_slots[index].calls, 0, 0);
    return TRUE;
}

BOOL inventory_argument_get(unsigned index, argument_sample *sample) {
    if (!sample || index >= ARG_SAMPLE_CAPACITY || !InterlockedCompareExchange(&g_argument_ready[index], 0, 0))
        return FALSE;
    *sample = g_arguments[index];
    return TRUE;
}

BOOL inventory_coin_override_get(coin_override *override) {
    if (!override || !InterlockedCompareExchange(&g_override_ready, 0, 0))
        return FALSE;
    *override = g_override;
    return TRUE;
}

inventory_stats inventory_totals(void) {
    return (inventory_stats){InterlockedCompareExchange64(&g_observed, 0, 0),
                             InterlockedCompareExchange64(&g_untracked, 0, 0),
                             InterlockedCompareExchange(&g_unique, 0, 0)};
}

void inventory_close(void) {
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_gate);
    g_accepting = FALSE;
    ReleaseSRWLockExclusive(&g_gate);
    SetLastError(saved_error);
}

#include "inventory.h"
#include <stdint.h>

typedef struct inventory_slot {
    PVOID volatile key;
    volatile LONG ready;
    volatile LONG64 calls __attribute__((aligned(8)));
    probe_sample first;
} inventory_slot;

static inventory_slot g_slots[INVENTORY_CAPACITY];
static SRWLOCK g_gate = SRWLOCK_INIT;
static BOOL g_accepting = TRUE;
static volatile LONG64 g_observed __attribute__((aligned(8)));
static volatile LONG64 g_untracked __attribute__((aligned(8)));
static volatile LONG g_unique;

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
                    InterlockedIncrement(&g_unique);
                    InterlockedExchange(&slot->ready, 1);
                }
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
    entry->calls = InterlockedCompareExchange64(&g_slots[index].calls, 0, 0);
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

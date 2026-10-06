#include "inventory.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int shared_function;
static DWORD WINAPI concurrent_hits(LPVOID context) {
    (void)context;
    probe_sample sample = {.function = &shared_function, .instance = &shared_function};
    for (unsigned i = 0; i < 2000; ++i) {
        SetLastError(0x1234);
        inventory_observe(&sample);
        if (GetLastError() != 0x1234)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "capacity") == 0) {
        int functions[INVENTORY_CAPACITY + 1];
        for (unsigned i = 0; i < INVENTORY_CAPACITY + 1; ++i) {
            probe_sample sample = {.function = &functions[i]};
            inventory_observe(&sample);
        }
        for (unsigned i = 0; i < INVENTORY_CAPACITY; ++i) {
            probe_sample sample = {.function = &functions[i]};
            inventory_observe(&sample);
        }
        inventory_close();
        inventory_stats stats = inventory_totals();
        LONG64 sum = 0;
        for (unsigned i = 0; i < INVENTORY_CAPACITY; ++i) {
            inventory_entry entry;
            if (!inventory_get(i, &entry) || entry.calls != 2)
                return fail("full table still counts previously admitted functions");
            sum += entry.calls;
        }
        if (stats.unique != INVENTORY_CAPACITY || stats.untracked != 1 || sum + stats.untracked != stats.observed)
            return fail("bounded table and exact untracked-call accounting");
        puts("PASS: table saturation, collision handling and accounted untracked call");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "concurrent") == 0) {
        HANDLE threads[4];
        for (unsigned i = 0; i < 4; ++i) {
            threads[i] = CreateThread(NULL, 0, concurrent_hits, NULL, 0, NULL);
            if (!threads[i])
                return fail("inventory producer creation");
        }
        if (WaitForMultipleObjects(4, threads, TRUE, 10000) != WAIT_OBJECT_0)
            return fail("concurrent inventory producers finish");
        for (unsigned i = 0; i < 4; ++i) {
            DWORD code;
            if (!GetExitCodeThread(threads[i], &code) || code)
                return fail("concurrent inventory LastError");
            CloseHandle(threads[i]);
        }
        inventory_close();
        inventory_stats stats = inventory_totals();
        if (stats.observed != 8000 || stats.unique != 1 || stats.untracked)
            return fail("concurrent first insertion and exact repeated-call counts");
        LONG64 sum = 0;
        unsigned rows = 0;
        for (unsigned i = 0; i < INVENTORY_CAPACITY; ++i) {
            inventory_entry entry;
            if (inventory_get(i, &entry)) {
                sum += entry.calls;
                ++rows;
            }
        }
        if (rows != 1 || sum != 8000)
            return fail("one complete published row after concurrent insertion");
        puts("PASS: 8000 concurrent duplicate hits, one function and exact counts");
        return 0;
    }
    int function, instance, other_instance;
    probe_sample sample = {.function = &function, .instance = &instance, .count = 3};
    SetLastError(0x1234);
    inventory_observe(&sample);
    sample.instance = &other_instance;
    inventory_observe(&sample);
    if (GetLastError() != 0x1234)
        return fail("inventory changed LastError");
    inventory_stats stats = inventory_totals();
    if (stats.observed != 2 || stats.unique != 1 || stats.untracked)
        return fail("duplicate function must increment a counter, not create another record");
    unsigned found = 0;
    for (unsigned i = 0; i < INVENTORY_CAPACITY; ++i) {
        inventory_entry entry;
        if (!inventory_get(i, &entry))
            continue;
        ++found;
        if (entry.first.function != &function || entry.first.instance != &instance || entry.calls != 2)
            return fail("first-seen metadata and occurrence count");
    }
    if (found != 1)
        return fail("one published inventory record");
    inventory_close();
    inventory_observe(&sample);
    if (inventory_totals().observed != 2)
        return fail("closed inventory must not admit a late producer");
    puts("PASS: first-seen function, duplicate counts, instance-independent key and frozen close");
    return 0;
}

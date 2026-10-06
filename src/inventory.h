#ifndef AVS_INVENTORY_H
#define AVS_INVENTORY_H
#include "probe.h"

#ifndef INVENTORY_CAPACITY
#define INVENTORY_CAPACITY 4096u
#endif

typedef struct inventory_entry {
    probe_sample first;
    LONG64 calls;
} inventory_entry;

typedef struct inventory_stats {
    LONG64 observed;
    LONG64 untracked;
    LONG unique;
} inventory_stats;

/* One capture per payload mapping. Pointers are opaque, provisional identity keys. */
void inventory_observe(const probe_sample *sample);
BOOL inventory_get(unsigned index, inventory_entry *entry);
inventory_stats inventory_totals(void);
/* Freeze metadata publication, not execution of the original function. */
void inventory_close(void);
#endif

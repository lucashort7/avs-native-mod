#ifndef AVS_INVENTORY_H
#define AVS_INVENTORY_H
#include "probe.h"
#include "function_name.h"
#include <stdint.h>

#ifndef INVENTORY_CAPACITY
#define INVENTORY_CAPACITY 4096u
#endif

typedef struct inventory_entry {
    probe_sample first;
    function_label label;
    LONG64 calls;
} inventory_entry;

typedef struct inventory_stats {
    LONG64 observed;
    LONG64 untracked;
    LONG unique;
} inventory_stats;

#define ARG_SAMPLE_CAPACITY 16u
typedef struct argument_value {
    BOOL readable;
    uint32_t type;
    uint64_t data;
} argument_value;
typedef struct argument_sample {
    const void *function;
    argument_value value[2];
} argument_sample;
typedef struct coin_override {
    const void *function;
    double original;
    double multiplied;
    BOOL success;
} coin_override;

/* One capture per payload mapping. Pointers are opaque, provisional identity keys. */
void inventory_observe(const probe_sample *sample);
BOOL inventory_get(unsigned index, inventory_entry *entry);
BOOL inventory_argument_get(unsigned index, argument_sample *sample);
BOOL inventory_coin_override_get(coin_override *override);
inventory_stats inventory_totals(void);
/* Freeze metadata publication, not execution of the original function. */
void inventory_close(void);
#endif

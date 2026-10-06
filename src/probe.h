#ifndef AVS_SCRIPT_PROBE_H
#define AVS_SCRIPT_PROBE_H

#include <windows.h>
#include <stddef.h>

/* Explicit machine-level ABI candidate, not a reconstructed C++ class.
 * RCX is return storage, RDX the function object, R8 the instance, R9 args.
 * The remaining three arguments occupy the entry stack. Live validation pending. */
typedef void *(*script_call_fn)(void *result, void *function, void *instance, const void *const *args, int count,
                                void *error, void *state);

#define PROBE_SAMPLE_CAPACITY 64u

typedef struct probe_sample {
    void *result;
    void *function;
    void *instance;
    const void *const *args;
    int count;
    void *error;
    void *state;
} probe_sample;

BOOL probe_prepare(void *target, const unsigned char *expected, size_t size);
BOOL probe_enable(void);
BOOL probe_stop(void);
BOOL probe_release_disabled(void);
BOOL probe_retained(void);
LONG64 probe_call_count(void);
BOOL probe_get_sample(unsigned index, probe_sample *sample);
/* Single writer only. final freezes metadata and emits occurrence totals. */
BOOL probe_write_inventory(BOOL final);
/* Caller owns activation and stop. This loop only writes batches and consumes .stop. */
BOOL probe_run_capture(const wchar_t *stop_path, DWORD flush_ms);

#endif

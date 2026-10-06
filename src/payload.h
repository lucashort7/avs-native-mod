#ifndef AVS_PAYLOAD_H
#define AVS_PAYLOAD_H
#include "probe.h"
/* Synchronous ABI. No threads, timers, registrations, retained sample pointers,
 * or recursive target calls may escape any payload operation. */
typedef BOOL(WINAPI *payload_start_fn)(const wchar_t *, const wchar_t *);
typedef void(WINAPI *payload_observe_fn)(const probe_sample *);
typedef BOOL(WINAPI *payload_stop_fn)(void);
typedef BOOL(WINAPI *payload_flush_fn)(void);
BOOL WINAPI avs_payload_start(const wchar_t *directory, const wchar_t *name);
void WINAPI avs_payload_observe(const probe_sample *sample);
BOOL WINAPI avs_payload_flush(void);
BOOL WINAPI avs_payload_stop(void);
#endif

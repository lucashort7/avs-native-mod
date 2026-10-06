#include "probe.h"
#include "logger.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int function_token;
static int instance_token;
static int state_token;
static int argument_token;
static volatile LONG original_calls;
static volatile LONG bad_forwarding;

/* This is a test function, not recovered game code. */
__attribute__((noinline)) static void *fixture_target(void *result, void *function, void *instance,
                                                     const void *const *args, int count,
                                                     void *error, void *state) {
    if (GetLastError() != 0x1234 || function != &function_token || instance != &instance_token ||
        args == NULL || args[0] != &argument_token || count != 3 || state != &state_token)
        InterlockedExchange(&bad_forwarding, 1);
    InterlockedIncrement(&original_calls);
    *(uint64_t *)result = UINT64_C(0x1122334455667788);
    *(int *)error = 17;
    SetLastError(0x4321);
    return result;
}

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static DWORD WINAPI stress_calls(LPVOID context) {
    unsigned iterations = *(unsigned *)context;
    script_call_fn volatile invoke = fixture_target;
    for (unsigned i = 0; i < iterations; ++i) {
        uint64_t result = 0;
        int error = 0;
        const void *args[] = {&argument_token};
        SetLastError(0x1234);
        void *returned = invoke(&result, &function_token, &instance_token, args, 3, &error, &state_token);
        if (GetLastError() != 0x4321 || returned != &result || error != 17 ||
            result != UINT64_C(0x1122334455667788)) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    unsigned char entry[32];
    script_call_fn volatile invoke = fixture_target;
    memcpy(entry, (const void *)fixture_target, sizeof entry);
    if (!logger_init(L".\\", L"hook-fixture")) return fail("logger init");
    if (argc == 2 && strcmp(argv[1], "guard") == 0) {
        entry[0] ^= 0xff;
        if (probe_prepare((void *)fixture_target, entry, sizeof entry)) return fail("mismatch accepted");
        if (probe_retained() || probe_call_count()) return fail("mismatch changed observation state");
        entry[0] ^= 0xff;
        if (memcmp(entry, (const void *)fixture_target, sizeof entry)) return fail("mismatch changed code");
        puts("PASS: mismatched entry rejected without modifying target code");
        return 0;
    }
    if (!probe_prepare((void *)fixture_target, entry, sizeof entry)) return fail("prepare");
    if (probe_call_count() != 0) return fail("created hook must stay disabled");
    if (argc == 2 && strcmp(argv[1], "disabled") == 0) {
        unsigned iterations = 1;
        if (stress_calls(&iterations) || original_calls != 1 || bad_forwarding || probe_call_count())
            return fail("never-enabled hook intercepted or changed the original");
        if (!probe_release_disabled() || probe_retained()) return fail("never-enabled cleanup");
        if (memcmp(entry, (const void *)fixture_target, sizeof entry)) return fail("cleanup changed code");
        puts("PASS: disabled hook preserves original execution and releases never-enabled resources");
        return 0;
    }
    if (!probe_enable()) return fail("enable");

    uint64_t result = 0;
    int error = 0;
    const void *args[] = {&argument_token};
    SetLastError(0x1234);
    void *returned = invoke(&result, &function_token, &instance_token, args, 3, &error, &state_token);
    DWORD last_error = GetLastError();
    if (probe_call_count() != 1) return fail("expected one detour observation after enabling");
    if (bad_forwarding || original_calls != 1 || returned != &result || error != 17 ||
        result != UINT64_C(0x1122334455667788) || last_error != 0x4321)
        return fail("unchanged arguments, original operation, return and LastError");
    probe_sample sample;
    if (!probe_get_sample(0, &sample) || sample.function != &function_token ||
        sample.instance != &instance_token || sample.result != &result || sample.args != args ||
        sample.count != 3 || sample.error != &error || sample.state != &state_token)
        return fail("captured arguments");
    if (!wlog("fixture observed: function=%p instance=%p argc=%d state=%p",
              sample.function, sample.instance, sample.count, sample.state)) return fail("file log");
    enum { THREADS = 4 };
    unsigned iterations = 128;
    HANDLE threads[THREADS];
    for (unsigned i = 0; i < THREADS; ++i) {
        threads[i] = CreateThread(NULL, 0, stress_calls, &iterations, 0, NULL);
        if (!threads[i]) return fail("stress thread creation");
    }
    if (WaitForMultipleObjects(THREADS, threads, TRUE, 10000) != WAIT_OBJECT_0)
        return fail("stress threads did not finish");
    for (unsigned i = 0; i < THREADS; ++i) {
        DWORD code = 1;
        if (!GetExitCodeThread(threads[i], &code) || code) return fail("stress forwarding");
        CloseHandle(threads[i]);
    }
    LONG expected = 1 + THREADS * iterations;
    if (original_calls != expected || probe_call_count() != expected || bad_forwarding)
        return fail("concurrent call accounting");
    for (unsigned i = 0; i < PROBE_SAMPLE_CAPACITY; ++i) {
        if (!probe_get_sample(i, &sample) || sample.function != &function_token ||
            sample.instance != &instance_token || sample.count != 3 || sample.state != &state_token)
            return fail("published concurrent sample");
    }
    if (probe_get_sample(PROBE_SAMPLE_CAPACITY, &sample)) return fail("sample bound");
    if (!probe_stop()) return fail("stop");
    SetLastError(0x1234);
    invoke(&result, &function_token, &instance_token, args, 3, &error, &state_token);
    if (probe_call_count() != expected || original_calls != expected + 1 || bad_forwarding)
        return fail("stopped hook must not intercept the next call");
    if (!probe_retained() || probe_release_disabled()) return fail("active-hook retention");
    printf("PASS: %ld intercepted calls; seven arguments, return storage, LastError, file log, concurrent bounded capture, stop and retention\n", expected);
    return 0;
}

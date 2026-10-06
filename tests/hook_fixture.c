#include "probe.h"
#include "logger.h"
#include "inventory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int function_token;
static int instance_token;
static int state_token;
static int argument_token;
static volatile LONG original_calls;
static volatile LONG bad_forwarding;
static volatile LONG pause_original;
static HANDLE original_entered;
static HANDLE original_release;

/* This is a test function, not recovered game code. */
__attribute__((noinline)) static void *fixture_target(void *result, void *function, void *instance,
                                                      const void *const *args, int count, void *error, void *state) {
    if (GetLastError() != 0x1234 || function != &function_token || instance != &instance_token || args == NULL ||
        args[0] != &argument_token || count != 3 || state != &state_token)
        InterlockedExchange(&bad_forwarding, 1);
    InterlockedIncrement(&original_calls);
    if (InterlockedExchange(&pause_original, 0)) {
        if (!SetEvent(original_entered) || WaitForSingleObject(original_release, 10000) != WAIT_OBJECT_0)
            InterlockedExchange(&bad_forwarding, 1);
    }
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
        if (GetLastError() != 0x4321 || returned != &result || error != 17 || result != UINT64_C(0x1122334455667788))
            return 1;
    }
    return 0;
}

static DWORD WINAPI capture_worker(LPVOID context) {
    (void)context;
    return (DWORD)probe_run_capture(L"hook-fixture.stop", 2000);
}

int main(int argc, char **argv) {
    unsigned char entry[32];
    script_call_fn volatile invoke = fixture_target;
    memcpy(entry, (const void *)fixture_target, sizeof entry);
    DeleteFileW(L"hook-fixture.log");
    DeleteFileW(L"hook-fixture.stop");
    if (!logger_init(L".\\", L"hook-fixture"))
        return fail("logger init");
    if (argc == 2 && strcmp(argv[1], "guard") == 0) {
        entry[0] ^= 0xff;
        if (probe_prepare((void *)fixture_target, entry, sizeof entry))
            return fail("mismatch accepted");
        if (probe_retained() || probe_call_count())
            return fail("mismatch changed observation state");
        entry[0] ^= 0xff;
        if (memcmp(entry, (const void *)fixture_target, sizeof entry))
            return fail("mismatch changed code");
        puts("PASS: mismatched entry rejected without modifying target code");
        return 0;
    }
    if (!probe_prepare((void *)fixture_target, entry, sizeof entry))
        return fail("prepare");
    if (probe_call_count() != 0)
        return fail("created hook must stay disabled");
    if (argc == 2 && strcmp(argv[1], "disabled") == 0) {
        unsigned iterations = 1;
        if (stress_calls(&iterations) || original_calls != 1 || bad_forwarding || probe_call_count())
            return fail("never-enabled hook intercepted or changed the original");
        if (!probe_release_disabled() || probe_retained())
            return fail("never-enabled cleanup");
        if (memcmp(entry, (const void *)fixture_target, sizeof entry))
            return fail("cleanup changed code");
        puts("PASS: disabled hook preserves original execution and releases never-enabled resources");
        return 0;
    }
    if (!probe_enable())
        return fail("enable");
    HANDLE capture = NULL;
    if (argc == 2 && strcmp(argv[1], "continuous") == 0) {
        capture = CreateThread(NULL, 0, capture_worker, NULL, 0, NULL);
        if (!capture)
            return fail("capture worker creation");
    }

    uint64_t result = 0;
    int error = 0;
    const void *args[] = {&argument_token};
    SetLastError(0x1234);
    void *returned = invoke(&result, &function_token, &instance_token, args, 3, &error, &state_token);
    DWORD last_error = GetLastError();
    if (probe_call_count() != 1)
        return fail("expected one detour observation after enabling");
    if (bad_forwarding || original_calls != 1 || returned != &result || error != 17 ||
        result != UINT64_C(0x1122334455667788) || last_error != 0x4321)
        return fail("unchanged arguments, original operation, return and LastError");
    probe_sample sample;
    if (!probe_get_sample(0, &sample) || sample.function != &function_token || sample.instance != &instance_token ||
        sample.result != &result || sample.args != args || sample.count != 3 || sample.error != &error ||
        sample.state != &state_token)
        return fail("captured arguments");
    if (!wlog("fixture observed: function=%p instance=%p argc=%d state=%p", sample.function, sample.instance,
              sample.count, sample.state))
        return fail("file log");
    enum { THREADS = 4 };
    unsigned iterations = 128;
    HANDLE threads[THREADS];
    for (unsigned i = 0; i < THREADS; ++i) {
        threads[i] = CreateThread(NULL, 0, stress_calls, &iterations, 0, NULL);
        if (!threads[i])
            return fail("stress thread creation");
    }
    if (WaitForMultipleObjects(THREADS, threads, TRUE, 10000) != WAIT_OBJECT_0)
        return fail("stress threads did not finish");
    for (unsigned i = 0; i < THREADS; ++i) {
        DWORD code = 1;
        if (!GetExitCodeThread(threads[i], &code) || code)
            return fail("stress forwarding");
        CloseHandle(threads[i]);
    }
    LONG expected = 1 + THREADS * iterations;
    if (original_calls != expected || probe_call_count() != expected || bad_forwarding)
        return fail("concurrent call accounting");
    inventory_stats stats = inventory_totals();
    if (stats.observed != expected || stats.unique != 1 || stats.untracked)
        return fail("native detour must feed continuous function inventory beyond the sample limit");
    if (capture) {
        Sleep(100);
        DWORD code;
        if (!GetExitCodeThread(capture, &code) || code != STILL_ACTIVE)
            return fail("continuous worker must stay alive beyond the sample limit");
        Sleep(5500);
        if (!GetExitCodeThread(capture, &code) || code != STILL_ACTIVE)
            return fail("continuous worker must not stop after five seconds");
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (!GetFileAttributesExW(L"hook-fixture.log", GetFileExInfoStandard, &attrs) || !attrs.nFileSizeLow)
            return fail("periodic flush wrote an actual file");
        HANDLE stop =
            CreateFileW(L"hook-fixture.stop", GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (stop == INVALID_HANDLE_VALUE || !CloseHandle(stop))
            return fail("create capture stop signal");
        if (WaitForSingleObject(capture, 3000) != WAIT_OBJECT_0 || !GetExitCodeThread(capture, &code) || code != TRUE)
            return fail("continuous capture consumes stop and returns successfully");
        CloseHandle(capture);
        if (GetFileAttributesW(L"hook-fixture.stop") != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            return fail("stop signal was consumed");
    }
    if (!probe_write_inventory(FALSE) || !probe_write_inventory(FALSE))
        return fail("worker writes new functions without repeating first-seen records");
    for (unsigned i = 0; i < PROBE_SAMPLE_CAPACITY; ++i) {
        if (!probe_get_sample(i, &sample) || sample.function != &function_token || sample.instance != &instance_token ||
            sample.count != 3 || sample.state != &state_token)
            return fail("published concurrent sample");
    }
    if (probe_get_sample(PROBE_SAMPLE_CAPACITY, &sample))
        return fail("sample bound");
    HANDLE pending = NULL;
    unsigned one_call = 1;
    if (argc == 2 && strcmp(argv[1], "inflight") == 0) {
        original_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
        original_release = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!original_entered || !original_release)
            return fail("in-flight test events");
        InterlockedExchange(&pause_original, 1);
        pending = CreateThread(NULL, 0, stress_calls, &one_call, 0, NULL);
        if (!pending || WaitForSingleObject(original_entered, 3000) != WAIT_OBJECT_0)
            return fail("original call pauses after its inventory publication");
        ++expected;
    }
    if (!probe_stop())
        return fail("stop");
    SetLastError(0x1234);
    invoke(&result, &function_token, &instance_token, args, 3, &error, &state_token);
    if (probe_call_count() != expected || original_calls != expected + 1 || bad_forwarding)
        return fail("stopped hook must not intercept the next call");
    if (!probe_retained() || probe_release_disabled())
        return fail("active-hook retention");
    if (!probe_write_inventory(TRUE) || !logger_close())
        return fail("final inventory drain");
    if (pending) {
        DWORD code;
        if (!GetExitCodeThread(pending, &code) || code != STILL_ACTIVE)
            return fail("logger closed while original is still in flight");
        if (!SetEvent(original_release) || WaitForSingleObject(pending, 3000) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(pending, &code) || code)
            return fail("retained in-flight original returns unchanged after logging stops");
        CloseHandle(pending);
        CloseHandle(original_entered);
        CloseHandle(original_release);
        if (inventory_totals().observed != expected)
            return fail("closed inventory remains stable after original resumes");
    }
    FILE *log = fopen("hook-fixture.log", "rb");
    char text[4096] = {0};
    if (!log)
        return fail("open actual inventory log");
    fread(text, 1, sizeof text - 1, log);
    fclose(log);
    char *first = strstr(text, "first_seen ");
    char count_text[64];
    snprintf(count_text, sizeof count_text, "calls=%ld\r\n", expected);
    if (!first || strstr(first + 1, "first_seen ") || !strstr(text, "function_total ") || !strstr(text, count_text) ||
        !strstr(text, "untracked_calls=0"))
        return fail("actual file contains one first-seen row and complete occurrence totals");
    printf("PASS: %ld intercepted calls; seven arguments, return storage, LastError, deduplicated inventory, "
           "stop and retention\n",
           expected);
    return 0;
}

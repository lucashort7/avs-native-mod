#include <windows.h>
#include "probe.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static volatile LONG calls;
static HANDLE entered, release_original;
static volatile LONG pause_original;
static BOOL(WINAPI *request_command)(DWORD, const wchar_t *);
static DWORD WINAPI unload_thread(LPVOID context) {
    (void)context;
    return request_command(2, NULL) ? 0 : 1;
}
__attribute__((noinline)) static void *target(void *r, void *f, void *i, const void *const *a, int n, void *e,
                                              void *s) {
    if (GetLastError() != 0x1234 || f != (void *)1 || i != (void *)2 || !a || a[0] != (void *)3 || n != 4 ||
        s != (void *)5)
        return NULL;
    InterlockedIncrement(&calls);
    if (InterlockedExchange(&pause_original, 0)) {
        SetEvent(entered);
        if (WaitForSingleObject(release_original, 10000) != WAIT_OBJECT_0)
            return NULL;
    }
    *(uint64_t *)r = UINT64_C(0x1122334455667788);
    *(int *)e = 17;
    SetLastError(0x4321);
    return r;
}
static DWORD WINAPI invoke(LPVOID context) {
    unsigned count = *(unsigned *)context;
    script_call_fn volatile fn = target;
    for (unsigned j = 0; j < count; ++j) {
        uint64_t result = 0;
        int error = 0;
        const void *args[] = {(void *)3};
        SetLastError(0x1234);
        if (fn(&result, (void *)1, (void *)2, args, 4, &error, (void *)5) != &result ||
            result != UINT64_C(0x1122334455667788) || error != 17 || GetLastError() != 0x4321)
            return 1;
    }
    return 0;
}
static int fail(const char *text) {
    fprintf(stderr, "FAIL: %s\n", text);
    return 1;
}
static BOOL touch(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    return file != INVALID_HANDLE_VALUE && CloseHandle(file);
}
static BOOL wait_status(DWORD(WINAPI *fn)(void), DWORD expected) {
    for (unsigned j = 0; j < 500; ++j) {
        if (fn() == expected)
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}
int main(int argc, char **argv) {
    /* Fixture process only: invalid-image tests must return errors, not modal UI.
     * Set before the resident controller starts; retain all inherited flags. */
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
    DeleteFileW(L"avs-native-mod-fixture.log");
    DeleteFileW(L"avs-bridge-fixture.log");
    BOOL controls = argc == 2 && (strcmp(argv[1], "controls") == 0 || strcmp(argv[1], "stale") == 0 ||
                                  strcmp(argv[1], "unsafe") == 0 || strcmp(argv[1], "conflict") == 0);
    if (controls) {
        CreateDirectoryW(L"reload-fixture", NULL);
        DeleteFileW(L"reload-fixture\\avs-native-mod.enable");
        DeleteFileW(L"reload-fixture\\avs-native-mod.unload");
        DeleteFileW(L"reload-fixture\\avs-native-mod.stop");
        DeleteFileW(L"reload-fixture\\avs-native-mod-fixture.log");
        RemoveDirectoryW(L"reload-fixture\\avs-native-mod.enable");
        if (!CopyFileW(L"avs-bridge-fixture.dll", L"reload-fixture\\avs-bridge-fixture.dll", FALSE) ||
            !CopyFileW(L"avs-native-mod.dll", L"reload-fixture\\avs-native-mod.dll", FALSE))
            return fail("fixture staging");
    }
    HMODULE bridge =
        LoadLibraryW(controls ? L".\\reload-fixture\\avs-bridge-fixture.dll" : L".\\avs-bridge-fixture.dll");
    if (!bridge)
        return fail("resident bridge DLL missing");
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(void *, const unsigned char *, size_t);
    } prepare = {.raw = GetProcAddress(bridge, "avs_bridge_fixture_prepare")};
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(DWORD, const wchar_t *);
    } command = {.raw = GetProcAddress(bridge, "avs_bridge_fixture_command")};
    if (!prepare.raw || !command.raw)
        return fail("bridge lifecycle exports missing");
    request_command = command.fn;
    unsigned char entry[32];
    memcpy(entry, (const void *)target, sizeof entry);
    if (argc == 2 && strcmp(argv[1], "stale") == 0) {
        if (!touch(L"reload-fixture\\avs-native-mod.enable") || prepare.fn((void *)target, entry, sizeof entry) ||
            memcmp(entry, (const void *)target, sizeof entry) || GetModuleHandleW(L"avs-native-mod.dll"))
            return fail("stale enable must refuse before hook preparation");
        DeleteFileW(L"reload-fixture\\avs-native-mod.enable");
        unsigned one = 1;
        if (invoke(&one))
            return fail("stale original forwarding");
        puts("PASS: stale startup control refused without target changes or payload load");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "guard") == 0) {
        entry[0] ^= 0xff;
        if (prepare.fn((void *)target, entry, sizeof entry))
            return fail("resident entry mismatch accepted");
        entry[0] ^= 0xff;
        unsigned one = 1;
        if (memcmp(entry, (const void *)target, sizeof entry) || invoke(&one))
            return fail("guard altered target");
        puts("PASS: resident bridge rejects mismatched entry bytes without altering target");
        return 0;
    }
    if (!prepare.fn((void *)target, entry, sizeof entry))
        return fail("prepare");
    if (argc == 2 && strcmp(argv[1], "readiness") == 0) {
        FILE *file = fopen("avs-bridge-fixture.log", "rb");
        char text[4096] = {0};
        if (!file)
            return fail("resident readiness record not flushed");
        fread(text, 1, sizeof text - 1, file);
        fclose(file);
        char address[80];
        snprintf(address, sizeof address, "target=%p", (void *)target);
        if (!strstr(text, "prepared DISABLED") || !strstr(text, "bridge pinned") || !strstr(text, address) ||
            GetModuleHandleW(L"avs-native-mod.dll") || memcmp(entry, (const void *)target, sizeof entry))
            return fail("readiness must name real target while payload is absent and entry is unpatched");
        puts("PASS: real startup diagnostic flushed before disabled readiness; exact target, resident pin, payload "
             "absent");
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "unsafe") == 0 || strcmp(argv[1], "conflict") == 0)) {
        union {
            FARPROC raw;
            DWORD(WINAPI *fn)(void);
        } state = {.raw = GetProcAddress(bridge, "script_probe_status")};
        unsigned one = 1;
        if (!command.fn(1, L"avs-native-mod.dll") || invoke(&one))
            return fail("control error active capture");
        BOOL conflict = strcmp(argv[1], "conflict") == 0;
        if (conflict) {
            if (!touch(L"reload-fixture\\avs-native-mod.enable"))
                return fail("enable conflicting with active capture");
        } else if (!CreateDirectoryW(L"reload-fixture\\avs-native-mod.enable", NULL))
            return fail("unsafe flag directory");
        if (!state.raw || !wait_status(state.fn, 4) || GetModuleHandleW(L"avs-native-mod.dll") || invoke(&one) ||
            command.fn(1, L"avs-native-mod.dll"))
            return fail("unsafe control must stop observation and quarantine");
        RemoveDirectoryW(L"reload-fixture\\avs-native-mod.enable");
        DeleteFileW(L"reload-fixture\\avs-native-mod.enable");
        DeleteFileW(L"reload-fixture\\avs-native-mod.unload");
        DeleteFileW(L"reload-fixture\\avs-native-mod.stop");
        puts("PASS: unsafe/conflicting controls close admission, unmap healthy payload and permanently block reload");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "failures") == 0) {
        unsigned one = 1;
        if (!(GetErrorMode() & SEM_FAILCRITICALERRORS))
            return fail("fixture loader error UI not suppressed");
        HANDLE invalid =
            CreateFileW(L"invalid-payload.dll", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written = 0;
        const char bytes[] = "This is not a PE DLL";
        if (invalid == INVALID_HANDLE_VALUE || !WriteFile(invalid, bytes, sizeof bytes, &written, NULL) ||
            written != sizeof bytes || !CloseHandle(invalid))
            return fail("invalid DLL file creation");
        if (command.fn(1, L"missing-payload.dll") || invoke(&one) || command.fn(1, L"invalid-payload.dll") ||
            invoke(&one) || command.fn(1, L"..\\avs-native-mod.dll") || command.fn(1, L"C:\\avs-native-mod.dll"))
            return fail("failed payload load altered original or accepted nonsibling path");
        DeleteFileW(L"invalid-payload.dll");
        if (!command.fn(1, L"avs-native-mod.dll") || invoke(&one) || !command.fn(2, NULL) || invoke(&one))
            return fail("recover after rejected payloads");
        puts("PASS: missing and invalid real DLLs rejected, path escape rejected, original unaffected, later valid "
             "cycle");
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "reference") == 0 || strcmp(argv[1], "pin") == 0)) {
        BOOL pin = strcmp(argv[1], "pin") == 0;
        unsigned one = 1;
        if (!command.fn(1, L"avs-native-mod.dll"))
            return fail("reference load");
        HMODULE extra;
        if (!GetModuleHandleExW(pin ? GET_MODULE_HANDLE_EX_FLAG_PIN : 0, L"avs-native-mod.dll", &extra))
            return fail("actual external reference/pin");
        if (command.fn(2, NULL) || !GetModuleHandleW(L"avs-native-mod.dll") || command.fn(1, L"avs-native-mod.dll") ||
            invoke(&one))
            return fail("successful FreeLibrary with remaining mapping must block reload");
        if (!pin &&
            (!FreeLibrary(extra) || GetModuleHandleW(L"avs-native-mod.dll") || command.fn(1, L"avs-native-mod.dll")))
            return fail("terminal quarantine persists after external release");
        puts("PASS: actual extra reference/pin prevents verified unmap; admission closed and reload permanently "
             "refused");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "cleanup") == 0) {
        unsigned one = 1;
        if (!command.fn(1, L"avs-payload-fixture.dll"))
            return fail("cleanup fixture load");
        union {
            FARPROC raw;
            void(WINAPI *fn)(void);
        } fault = {.raw =
                       GetProcAddress(GetModuleHandleW(L"avs-payload-fixture.dll"), "avs_payload_fixture_fail_flush")};
        if (!fault.raw)
            return fail("checked payload flush fault missing");
        fault.fn();
        if (invoke(&one) || command.fn(2, NULL) || !GetModuleHandleW(L"avs-payload-fixture.dll") ||
            command.fn(1, L"avs-native-mod.dll") || invoke(&one))
            return fail("cleanup error must retain payload and block replacement");
        puts("PASS: real payload logger zero-progress fault, cleanup refused, mapping retained, original still "
             "forwards");
        return 0;
    }
    if (controls) {
        union {
            FARPROC raw;
            DWORD(WINAPI *fn)(void);
        } state = {.raw = GetProcAddress(bridge, "script_probe_status")};
        unsigned one = 1;
        if (!state.raw || !touch(L"reload-fixture\\avs-native-mod.enable") || !wait_status(state.fn, 7))
            return fail("automatic enable sentinel not consumed");
        if (GetFileAttributesW(L"reload-fixture\\avs-native-mod.enable") != INVALID_FILE_ATTRIBUTES || invoke(&one))
            return fail("enable forwarding and consumed control");
        if (!touch(L"reload-fixture\\avs-native-mod.unload") || !wait_status(state.fn, 2) ||
            GetModuleHandleW(L"avs-native-mod.dll"))
            return fail("automatic unload and module absence");
        if (!CopyFileW(L"avs-payload-fixture.dll", L"reload-fixture\\avs-native-mod.dll", FALSE))
            return fail("replace unmapped payload at same path");
        if (!touch(L"reload-fixture\\avs-native-mod.enable") || !wait_status(state.fn, 7) || invoke(&one) ||
            !touch(L"reload-fixture\\avs-native-mod.stop") || !wait_status(state.fn, 2) ||
            GetModuleHandleW(L"avs-native-mod.dll"))
            return fail("replacement enable and stop alias");
        FILE *file = fopen("reload-fixture/avs-native-mod-fixture.log", "rb");
        char text[4096] = {0};
        if (!file)
            return fail("replacement file output");
        fread(text, 1, sizeof text - 1, file);
        fclose(file);
        if (!strstr(text, "generation=1") || !strstr(text, "generation=2") || calls != 2)
            return fail("new payload implementation must run at the same sibling path");
        puts("PASS: automatic sentinel cycles, absolute sibling loading, binary replacement, fresh generation output");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "gate") == 0) {
        union {
            FARPROC raw;
            void(WINAPI *fn)(HANDLE, HANDLE, HANDLE);
        } seam = {.raw = GetProcAddress(bridge, "avs_bridge_fixture_barriers")};
        if (!seam.raw)
            return fail("resident gate barriers missing");
        HANDLE observed = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE resume_observer = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE closed = CreateEventW(NULL, TRUE, FALSE, NULL);
        seam.fn(NULL, NULL, closed);
        if (!command.fn(1, L"avs-payload-fixture.dll"))
            return fail("fixture payload load");
        HMODULE payload = GetModuleHandleW(L"avs-payload-fixture.dll");
        union {
            FARPROC raw;
            void(WINAPI *fn)(HANDLE, HANDLE);
        } pause = {.raw = GetProcAddress(payload, "avs_payload_fixture_pause")};
        if (!pause.raw)
            return fail("actual payload observer barrier missing");
        pause.fn(observed, resume_observer);
        unsigned one = 1;
        HANDLE pending = CreateThread(NULL, 0, invoke, &one, 0, NULL);
        if (!pending || WaitForSingleObject(observed, 3000) != WAIT_OBJECT_0)
            return fail("observer not admitted");
        HANDLE stop = CreateThread(NULL, 0, unload_thread, NULL, 0, NULL);
        if (!stop || WaitForSingleObject(closed, 3000) != WAIT_OBJECT_0)
            return fail("admission not atomically closed");
        if (WaitForSingleObject(stop, 0) != WAIT_TIMEOUT || !GetModuleHandleW(L"avs-payload-fixture.dll"))
            return fail("unload must wait for admitted observer");
        unsigned many = 10000;
        HANDLE traffic[4];
        for (unsigned j = 0; j < 4; ++j) {
            traffic[j] = CreateThread(NULL, 0, invoke, &many, 0, NULL);
            if (!traffic[j])
                return fail("traffic thread");
        }
        if (!SetEvent(resume_observer) || WaitForSingleObject(pending, 3000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(stop, 3000) != WAIT_OBJECT_0 ||
            WaitForMultipleObjects(4, traffic, TRUE, 10000) != WAIT_OBJECT_0)
            return fail("drain and traffic finish");
        DWORD code;
        if (!GetExitCodeThread(stop, &code) || code || GetModuleHandleW(L"avs-payload-fixture.dll"))
            return fail("admitted observer drained before actual unmap");
        if (!GetExitCodeThread(pending, &code) || code)
            return fail("admitted forwarding");
        for (unsigned j = 0; j < 4; ++j) {
            if (!GetExitCodeThread(traffic[j], &code) || code)
                return fail("concurrent forwarding");
            CloseHandle(traffic[j]);
        }
        CloseHandle(pending);
        CloseHandle(stop);
        CloseHandle(observed);
        CloseHandle(resume_observer);
        HANDLE before = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE resume_before = CreateEventW(NULL, TRUE, FALSE, NULL);
        ResetEvent(closed);
        seam.fn(before, resume_before, closed);
        if (!command.fn(1, L"avs-native-mod.dll"))
            return fail("reload for pre-gate pause");
        pending = CreateThread(NULL, 0, invoke, &one, 0, NULL);
        if (!pending || WaitForSingleObject(before, 3000) != WAIT_OBJECT_0)
            return fail("resident pre-gate pause");
        if (!command.fn(2, NULL) || GetModuleHandleW(L"avs-native-mod.dll"))
            return fail("pre-gate payload unmap");
        if (!SetEvent(resume_before) || WaitForSingleObject(pending, 3000) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(pending, &code) || code || calls != 40002)
            return fail("pre-gate resumes into resident only");
        CloseHandle(pending);
        CloseHandle(before);
        CloseHandle(resume_before);
        CloseHandle(closed);
        FILE *file = fopen("avs-native-mod-fixture.log", "rb");
        char text[4096] = {0};
        if (!file)
            return fail("gate output file");
        fread(text, 1, sizeof text - 1, file);
        fclose(file);
        if (!strstr(text, "observed=1") || !strstr(text, "observed=0"))
            return fail("gate inventory exact cycles");
        puts("PASS: admitted observer blocks unload, 40000 concurrent forwarded calls, pre-gate pause survives actual "
             "unmap");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "repeat") == 0) {
        unsigned one = 1;
        for (unsigned cycle = 0; cycle < 32; ++cycle) {
            if (GetModuleHandleW(L"avs-native-mod.dll") || !command.fn(1, L"avs-native-mod.dll") || invoke(&one) ||
                !command.fn(2, NULL) || GetModuleHandleW(L"avs-native-mod.dll") || invoke(&one))
                return fail("repeated fresh mapping, unmap and forwarding");
        }
        if (calls != 64)
            return fail("repeated original call count");
        puts("PASS: 32 same-process payload mappings and verified unmaps; 64 original calls");
        return 0;
    }
    if (!command.fn(1, L"avs-native-mod.dll"))
        return fail("prepare and load");
    unsigned one = 1;
    if (invoke(&one))
        return fail("forwarding");
    entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    release_original = CreateEventW(NULL, TRUE, FALSE, NULL);
    InterlockedExchange(&pause_original, 1);
    HANDLE pending = CreateThread(NULL, 0, invoke, &one, 0, NULL);
    if (!pending || WaitForSingleObject(entered, 3000) != WAIT_OBJECT_0)
        return fail("original not in flight");
    SetLastError(0x1234);
    if (!command.fn(2, NULL) || GetLastError() != 0x1234 || GetModuleHandleW(L"avs-native-mod.dll"))
        return fail("payload must unmap while original remains in flight");
    DWORD code;
    if (!GetExitCodeThread(pending, &code) || code != STILL_ACTIVE)
        return fail("original unexpectedly finished");
    if (invoke(&one))
        return fail("unloaded forwarding");
    if (!SetEvent(release_original) || WaitForSingleObject(pending, 3000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(pending, &code) || code)
        return fail("original return into resident code");
    CloseHandle(pending);
    CloseHandle(entered);
    CloseHandle(release_original);
    if (!command.fn(1, L"avs-native-mod.dll") || invoke(&one) || !command.fn(2, NULL) ||
        GetModuleHandleW(L"avs-native-mod.dll"))
        return fail("same-process reload cycle");
    if (!FreeLibrary(bridge) || !GetModuleHandleW(L"avs-bridge-fixture.dll"))
        return fail("bridge must stay pinned");
    FILE *file = fopen("avs-native-mod-fixture.log", "rb");
    char text[4096] = {0};
    if (!file)
        return fail("actual file missing");
    fread(text, 1, sizeof text - 1, file);
    fclose(file);
    if (!strstr(text, "observed=2") || !strstr(text, "observed=1") || calls != 4)
        return fail("per-cycle actual inventory");
    puts(
        "PASS: actual MinHook, payload unload with long original in flight, same-process reload, resident pin, output");
    return 0;
}

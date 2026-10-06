#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static BOOL WINAPI partial_write(HANDLE file, LPCVOID text, DWORD size, LPDWORD written, LPOVERLAPPED overlap) {
    return WriteFile(file, text, size > 7 ? 7 : size, written, overlap);
}

static DWORD WINAPI produce_records(LPVOID context) {
    unsigned producer = (unsigned)(uintptr_t)context;
    for (unsigned i = 0; i < 200; ++i) {
        SetLastError(0x1234);
        if (!wlog("producer=%u row=%u", producer, i) || GetLastError() != 0x1234)
            return 1;
    }
    return 0;
}

static unsigned fault_calls;
static BOOL WINAPI failed_write(HANDLE file, LPCVOID text, DWORD size, LPDWORD written, LPOVERLAPPED overlap) {
    if (!fault_calls++)
        return WriteFile(file, text, size > 7 ? 7 : size, written, overlap);
    *written = 0;
    return FALSE;
}
static BOOL WINAPI zero_write(HANDLE file, LPCVOID text, DWORD size, LPDWORD written, LPOVERLAPPED overlap) {
    (void)file;
    (void)text;
    (void)size;
    (void)overlap;
    *written = 0;
    return TRUE;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "invalid") == 0) {
        SetLastError(0x1234);
        if (logger_init(L".\\missing-fixture-directory\\", L"logger") || logger_init(NULL, L"logger") ||
            logger_init(L".\\", NULL) || wlog("must not open a fallback file") || GetLastError() != 0x1234)
            return fail("invalid initialization fails without fallback and preserves LastError");
        puts("PASS: invalid paths and initialization fail explicitly");
        return 0;
    }
    DeleteFileW(L"logger-fixture.log");
    SetLastError(0x1234);
    if (!logger_init(L".\\", L"logger-fixture") || GetLastError() != 0x1234)
        return fail("logger initialization and LastError");
    if (!wlog("persistent handle fixture") || GetLastError() != 0x1234)
        return fail("record submission and LastError");
    if (argc == 2 && (strcmp(argv[1], "failure") == 0 || strcmp(argv[1], "zero") == 0)) {
        BOOL zero = strcmp(argv[1], "zero") == 0;
        logger_test_set_writer(zero ? zero_write : failed_write);
        SetLastError(0x1234);
        if (logger_flush() || wlog("no retry or duplicate prefix") || logger_flush() || logger_close() ||
            GetLastError() != 0x1234)
            return fail("write failure must be sticky, checked, finite and preserve LastError");
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (!GetFileAttributesExW(L"logger-fixture.log", GetFileExInfoStandard, &attrs) || attrs.nFileSizeHigh ||
            attrs.nFileSizeLow != (zero ? 0u : 7u))
            return fail("failed batch does not duplicate its partial prefix");
        HANDLE exclusive =
            CreateFileW(L"logger-fixture.log", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (exclusive == INVALID_HANDLE_VALUE)
            return fail("shutdown closes the file even after a write failure");
        CloseHandle(exclusive);
        puts("PASS: checked write failure, no duplicate retry, LastError and file close");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "stress") == 0) {
        HANDLE threads[4];
        for (unsigned i = 0; i < 4; ++i) {
            threads[i] = CreateThread(NULL, 0, produce_records, (LPVOID)(uintptr_t)i, 0, NULL);
            if (!threads[i])
                return fail("logger producer creation");
        }
        if (WaitForMultipleObjects(4, threads, TRUE, 10000) != WAIT_OBJECT_0)
            return fail("logger producers did not finish");
        for (unsigned i = 0; i < 4; ++i) {
            DWORD code;
            if (!GetExitCodeThread(threads[i], &code) || code)
                return fail("batch must accept concurrent records beyond its byte capacity");
            CloseHandle(threads[i]);
        }
        char oversized[2048];
        memset(oversized, 'X', sizeof oversized - 1);
        oversized[sizeof oversized - 1] = '\0';
        if (!wlog("%s", oversized) || !logger_close())
            return fail("bounded oversized record and final drain");
        FILE *file = fopen("logger-fixture.log", "rb");
        if (!file)
            return fail("read actual concurrent log");
        BOOL seen[4][200] = {{FALSE}};
        char line[600];
        unsigned rows = 0, lines = 0;
        while (fgets(line, sizeof line, file)) {
            size_t size = strlen(line);
            if (size < 2 || size >= 512 || line[size - 2] != '\r' || line[size - 1] != '\n')
                return fail("bounded intact CRLF records");
            ++lines;
            char *record = strstr(line, "producer=");
            if (record) {
                unsigned producer, row;
                if (sscanf(record, "producer=%u row=%u", &producer, &row) != 2 || producer >= 4 || row >= 200 ||
                    seen[producer][row])
                    return fail("duplicate, corrupt or out-of-range record");
                seen[producer][row] = TRUE;
                ++rows;
            }
        }
        fclose(file);
        if (rows != 800 || lines != 802)
            return fail("all concurrent and oversized records reached disk");
        puts("PASS: 800 concurrent records, bounded batches, oversized line and final drain");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "buffered") == 0) {
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (!GetFileAttributesExW(L"logger-fixture.log", GetFileExInfoStandard, &attrs) || attrs.nFileSizeHigh ||
            attrs.nFileSizeLow)
            return fail("records must wait in memory until batch flush");
        puts("PASS: records stay buffered before flush");
        return 0;
    }
    if (argc == 2 &&
        (strcmp(argv[1], "flush") == 0 || strcmp(argv[1], "partial") == 0 || strcmp(argv[1], "append") == 0)) {
        if (strcmp(argv[1], "partial") == 0)
            logger_test_set_writer(partial_write);
        SetLastError(0x1234);
        if (!logger_flush() || GetLastError() != 0x1234)
            return fail("batch flush and LastError");
        if (!wlog("final drain fixture") || !logger_close() || GetLastError() != 0x1234)
            return fail("shutdown drains the last batch and preserves LastError");
        if (strcmp(argv[1], "append") == 0 && (!logger_init(L".\\", L"logger-fixture") || !wlog("append fixture") ||
                                               !logger_close() || GetLastError() != 0x1234))
            return fail("reopening appends without truncating earlier records");
        FILE *file = fopen("logger-fixture.log", "rb");
        char text[1024] = {0};
        if (!file)
            return fail("open actual flushed log");
        size_t count = fread(text, 1, sizeof text - 1, file);
        fclose(file);
        if (!count || !strstr(text, "persistent handle fixture\r\n") || !strstr(text, "final drain fixture\r\n"))
            return fail("actual file contains both complete records");
        if (strcmp(argv[1], "append") == 0 && !strstr(text, "append fixture\r\n"))
            return fail("appended record reached disk without losing earlier records");
        puts("PASS: batch flush, final drain and LastError");
        return 0;
    }
    HANDLE exclusive =
        CreateFileW(L"logger-fixture.log", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (exclusive != INVALID_HANDLE_VALUE) {
        CloseHandle(exclusive);
        return fail("logger must keep its file handle open between records");
    }
    if (GetLastError() != ERROR_SHARING_VIOLATION)
        return fail("unexpected exclusive-open failure");
    puts("PASS: logger retains its file handle between records");
    return 0;
}

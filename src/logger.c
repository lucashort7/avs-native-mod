#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <strsafe.h>
#include <string.h>

static HANDLE g_file = INVALID_HANDLE_VALUE;
static char g_batch[8192];
static size_t g_used;
static BOOL g_healthy;
#ifdef AVS_LOGGER_TESTING
static logger_test_writer g_write = WriteFile;
void logger_test_set_writer(logger_test_writer writer) { g_write = writer ? writer : WriteFile; }
#else
#define g_write WriteFile
#endif

/* Append complete, bounded lines to <DLL basename>.log. Keep file I/O out of
 * DllMain. The lock serializes writes from threads using this logger. */
static SRWLOCK g_log_lock = SRWLOCK_INIT;

static BOOL flush_locked(void) {
    if (g_file == INVALID_HANDLE_VALUE || !g_healthy)
        return FALSE;
    size_t offset = 0;
    while (offset < g_used) {
        DWORD written = 0;
        DWORD remaining = (DWORD)(g_used - offset);
        if (!g_write(g_file, g_batch + offset, remaining, &written, NULL) || !written || written > remaining) {
            /* A prefix may already be on disk. Never retry it as a new batch. */
            g_healthy = FALSE;
            g_used = 0;
            return FALSE;
        }
        offset += written;
    }
    g_used = 0;
    return TRUE;
}

BOOL logger_flush(void) {
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_log_lock);
    BOOL success = flush_locked();
    ReleaseSRWLockExclusive(&g_log_lock);
    SetLastError(saved_error);
    return success;
}

BOOL logger_close(void) {
    DWORD saved_error = GetLastError();
    AcquireSRWLockExclusive(&g_log_lock);
    BOOL success = TRUE;
    if (g_file != INVALID_HANDLE_VALUE) {
        BOOL flushed = flush_locked();
        BOOL closed = CloseHandle(g_file);
        if (closed)
            g_file = INVALID_HANDLE_VALUE;
        success = flushed && closed;
    }
    ReleaseSRWLockExclusive(&g_log_lock);
    SetLastError(saved_error);
    return success;
}

BOOL logger_init(const wchar_t *dirpath, const wchar_t *name) {
    DWORD saved_error = GetLastError();
    BOOL success = FALSE;
    wchar_t path[MAX_PATH];
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_file == INVALID_HANDLE_VALUE && dirpath && name && *name &&
        SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%ls%ls.log", dirpath, name))) {
        g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
        success = g_file != INVALID_HANDLE_VALUE;
        if (success) {
            g_used = 0;
            g_healthy = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&g_log_lock);
    SetLastError(saved_error);
    return success;
}

BOOL wlog(const char *fmt, ...) {
    DWORD saved_error = GetLastError();
    BOOL success = FALSE;
    char buf[512];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int prefix =
        snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u ", (unsigned int)t.wYear, (unsigned int)t.wMonth,
                 (unsigned int)t.wDay, (unsigned int)t.wHour, (unsigned int)t.wMinute, (unsigned int)t.wSecond);
    if (prefix < 0 || (size_t)prefix >= sizeof(buf) - 2)
        goto done;
    /* Reserve two bytes for CRLF; vsnprintf also reserves its null terminator. */
    size_t capacity = sizeof(buf) - (size_t)prefix - 2;
    va_list ap;
    va_start(ap, fmt);
    int message = vsnprintf(buf + prefix, capacity, fmt, ap);
    va_end(ap);
    if (message < 0)
        goto done;
    size_t length = (size_t)prefix + ((size_t)message < capacity ? (size_t)message : capacity - 1);
    buf[length++] = '\r';
    buf[length++] = '\n';

    AcquireSRWLockExclusive(&g_log_lock);
    if (g_file != INVALID_HANDLE_VALUE && g_healthy && length > sizeof g_batch - g_used)
        flush_locked();
    if (g_file != INVALID_HANDLE_VALUE && g_healthy && length <= sizeof g_batch - g_used) {
        memcpy(g_batch + g_used, buf, length);
        g_used += length;
        success = TRUE;
    }
    ReleaseSRWLockExclusive(&g_log_lock);
done:
    /* Logging must not replace the calling thread's previous Win32 error. */
    SetLastError(saved_error);
    return success;
}

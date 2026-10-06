#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <strsafe.h>

const wchar_t *g_dirpath;
const wchar_t *g_name;

/* Append complete, bounded lines to <DLL basename>.log. Keep file I/O out of
 * DllMain. The lock serializes writes from threads using this logger. */
static SRWLOCK g_log_lock = SRWLOCK_INIT;

BOOL logger_init(const wchar_t *dirpath, const wchar_t *name) {
    g_dirpath = dirpath;
    g_name = name;

    return TRUE;
}

BOOL wlog(const char *fmt, ...) {
    DWORD saved_error = GetLastError();
    BOOL success = FALSE;
    wchar_t path[MAX_PATH];
    char buf[512];
    SYSTEMTIME t;
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%ls%ls.log", g_dirpath, g_name))) goto done;
    GetLocalTime(&t);
    int prefix =
        snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u ", (unsigned int)t.wYear, (unsigned int)t.wMonth,
                 (unsigned int)t.wDay, (unsigned int)t.wHour, (unsigned int)t.wMinute, (unsigned int)t.wSecond);
    if (prefix < 0 || (size_t)prefix >= sizeof(buf) - 2) goto done;
    /* Reserve two bytes for CRLF; vsnprintf also reserves its null terminator. */
    size_t capacity = sizeof(buf) - (size_t)prefix - 2;
    va_list ap;
    va_start(ap, fmt);
    int message = vsnprintf(buf + prefix, capacity, fmt, ap);
    va_end(ap);
    if (message < 0) goto done;
    size_t length = (size_t)prefix + ((size_t)message < capacity ? (size_t)message : capacity - 1);
    buf[length++] = '\r';
    buf[length++] = '\n';

    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        success = WriteFile(h, buf, (DWORD)length, &written, NULL) && written == (DWORD)length;
        if (!CloseHandle(h)) success = FALSE;
    }
    ReleaseSRWLockExclusive(&g_log_lock);
done:
    /* Logging must not replace the calling thread's previous Win32 error. */
    SetLastError(saved_error);
    return success;
}

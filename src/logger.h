#ifndef AVS_LOGGER_H
#define AVS_LOGGER_H
#include <windows.h>
BOOL logger_init(const wchar_t *dirpath, const wchar_t *name);
BOOL wlog(const char *fmt, ...);
BOOL logger_flush(void);
BOOL logger_close(void);
#ifdef AVS_LOGGER_TESTING
typedef BOOL(WINAPI *logger_test_writer)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
void logger_test_set_writer(logger_test_writer writer);
#endif
#endif

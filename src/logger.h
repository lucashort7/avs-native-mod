#ifndef AVS_LOGGER_H
#define AVS_LOGGER_H
#include <windows.h>
BOOL logger_init(const wchar_t *dirpath, const wchar_t *name);
BOOL wlog(const char *fmt, ...);
#endif

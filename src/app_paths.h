#ifndef LCB_APP_PATHS_H
#define LCB_APP_PATHS_H
#include <windows.h>
/* Reuse legacy storage in place, including its existing file lock. */
int app_data_root(const wchar_t *base, wchar_t root[MAX_PATH]);
#endif

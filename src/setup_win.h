#ifndef LTS_SETUP_WIN_H
#define LTS_SETUP_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Checks local paths only; never starts an engine or contacts a download site. */
int setup_paths_ready(const wchar_t *engine, const wchar_t *model);
int setup_dialog(HWND owner, HFONT font, wchar_t engine[MAX_PATH], wchar_t model[MAX_PATH]);
#endif

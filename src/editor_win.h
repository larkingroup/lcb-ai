#ifndef EDITOR_WIN_H
#define EDITOR_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int edit_workspace(HWND owner, HFONT font, const wchar_t *title,
    wchar_t *name, size_t namecap, wchar_t *prompt, size_t promptcap);
#endif

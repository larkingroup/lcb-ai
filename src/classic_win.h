#ifndef LCB_CLASSIC_WIN_H
#define LCB_CLASSIC_WIN_H
#include <windows.h>
#include <commctrl.h>

#include "classic_glyphs.h"

/* One small, DPI-scaled image list per application; no files or image codecs. */
HIMAGELIST classic_icons(int dpi);
void classic_edge(HDC dc, RECT r, int raised);
#endif

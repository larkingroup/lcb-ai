#ifndef LTI_CLASSIC_WIN_H
#define LTI_CLASSIC_WIN_H
#include <windows.h>
#include <commctrl.h>

enum { ClassicDocument, ClassicFolder, ClassicRun, ClassicStop,
    ClassicEngine, ClassicModel, ClassicOutput, ClassicSettings, ClassicIconCount };

/* One small, DPI-scaled image list per application; no files or image codecs. */
HIMAGELIST classic_icons(int dpi);
void classic_edge(HDC dc, RECT r, int raised);
#endif

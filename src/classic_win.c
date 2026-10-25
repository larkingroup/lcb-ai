#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define LCB_CLASSIC_GLYPH_DATA
#include "classic_win.h"

static COLORREF color(char c)
{
    unsigned long rgb=classic_glyph_rgb(c);
    if(c=='.') return RGB(255,0,255);
    return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);
}

HIMAGELIST classic_icons(int dpi)
{
    int size=MulDiv(16,dpi,96),i,x,y;
    HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,size,size);
    HGDIOBJ old;
    HIMAGELIST images=ImageList_Create(size,size,ILC_COLOR24|ILC_MASK,ClassicIconCount,0);
    ReleaseDC(NULL,screen);
    if(!dc || !bitmap || !images) {
        if(dc) DeleteDC(dc);
        if(bitmap) DeleteObject(bitmap);
        if(images) ImageList_Destroy(images);
        return NULL;
    }
    old=SelectObject(dc,bitmap);
    for(i=0;i<ClassicIconCount;i++) {
        for(y=0;y<16;y++) for(x=0;x<16;x++) {
            RECT r={MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(x+1,dpi,96),MulDiv(y+1,dpi,96)};
            SetDCBrushColor(dc,color(classic_glyphs[i][y][x]));
            FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH));
        }
        /* The image-list API selects this bitmap into its own DC. */
        SelectObject(dc,old);
        if(ImageList_AddMasked(images,bitmap,RGB(255,0,255))<0) {
            ImageList_Destroy(images); images=NULL; break;
        }
        SelectObject(dc,bitmap);
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
    return images;
}

void classic_edge(HDC dc, RECT r, int raised)
{
    DrawEdge(dc,&r,raised?EDGE_RAISED:EDGE_SUNKEN,BF_RECT);
}

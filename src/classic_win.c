#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "classic_win.h"

/* Hand-drawn 16px glyphs. Dot pixels become the image-list transparency mask. */
static const char glyphs[ClassicIconCount][16][17] = {
    {
        "................", "...kkkkkkkk.....", "...kwwwwwwbk....", "...kwwwwwwbbk...",
        "...kwwwwwwkkkk..", "...kwwwwwwwwwk..", "...kwbbbbbbwwk..", "...kwwwwwwwwwk..",
        "...kwbbbbbbwwk..", "...kwwwwwwwwwk..", "...kwbbbbwwwwk..", "...kwwwwwwwwwk..",
        "...kwwwwwwwwwk..", "...kwwwwwwwwwk..", "...kkkkkkkkkkk..", "................"
    }, {
        "................", "................", "..kkkkkk........", "..kyyyykk.......",
        "..kyyyyykkkkkk..", "..kyyyyyyyyyyk..", ".kkkkkkkkkkkkkk.", ".kyYYYYYYYYYYYk.",
        ".kyYYYYYYYYYYYk.", ".kyYYYYYYYYYYyk.", ".kyYYYYYYYYYYyk.", ".kyYYYYYYYYYyyk.",
        ".kyyyyyyyyyyyyk.", ".kkkkkkkkkkkkkk.", "................", "................"
    }, {
        "................", "....k...........", "....kk..........", "....kGk.........",
        "....kwGk........", "....kwGGk.......", "....kwGGGk......", "....kwGGGGk.....",
        "....kwGGGGgk....", "....kwGGGgk.....", "....kwGGgk......", "....kwGgk.......",
        "....kGgk........", "....kgk.........", "....kk..........", "................"
    }, {
        "................", "................", "..kkkkkkkkkkkk..", "..kwwwwwwwwwrk..",
        "..kwRRRRRRRRrk..", "..kwRRRRRRRRrk..", "..kwRRRRRRRRrk..", "..kwRRRRRRRRrk..",
        "..kwRRRRRRRRrk..", "..kwRRRRRRRRrk..", "..kwRRRRRRRRrk..", "..kwRRRRRRRRrk..",
        "..krrrrrrrrrrk..", "..kkkkkkkkkkkk..", "................", "................"
    }, {
        "................", "...k.k.k.k.k....", "..kkkkkkkkkkk...", "..kbbbbbbbbbk...",
        ".kkbwwwwwwwbkk..", "..kbwBBBBBwbk...", ".kkbwBBBBBwbkk..", "..kbwBBBBBwbk...",
        ".kkbwBBBBBwbkk..", "..kbwBBBBBwbk...", ".kkbwwwwwwwbkk..", "..kbbbbbbbbbk...",
        "..kkkkkkkkkkk...", "...k.k.k.k.k....", "................", "................"
    }, {
        "................", "....kkkkkkkk....", "..kkwwwwwwwwkk..", "..kBBBBBBBBBbk..",
        "..kbBBBBBBBbbk..", "..kkbbbbbbbbkk..", "..kbkkkkkkkkbk..", "..kwBBBBBBBbbk..",
        "..kkbbbbbbbbkk..", "..kbkkkkkkkkbk..", "..kwBBBBBBBbbk..", "..kwBBBBBBBbbk..",
        "..kkbbbbbbbbkk..", "....kkkkkkkk....", "................", "................"
    }, {
        "................", "................", ".kkkkkkkkkkkkkk.", ".kbbbbbbbbbbbbk.",
        ".kbkkkkkkkkkkbk.", ".kbkwwwwwwwwkbk.", ".kbkwGwwwwwwkbk.", ".kbkwwGwwwwwkbk.",
        ".kbkwGwwwwwwkbk.", ".kbkwwwwGGwwkbk.", ".kbkwwwwwwwwkbk.", ".kbkkkkkkkkkkbk.",
        ".kkkkkkkkkkkkkk.", "......kkkk......", "....kkkkkkkk....", "................"
    }, {
        "................", "....kk....kk....", "....kbk..kbk....", "....kbk..kbk....",
        "....kbkkkkbk....", "....kbwwwwbk....", ".....kbwwbk.....", "......kbbk......",
        ".....kbwk.......", "....kbwk........", "...kbwk.........", "..kbwk..........",
        ".kbwk...........", ".kbk............", "..k.............", "................"
    }
};

static COLORREF color(char c)
{
    switch(c) {
    case 'k': return RGB(74,83,91);
    case 'w': return RGB(255,255,245);
    case 'b': return RGB(148,179,201);
    case 'B': return RGB(65,117,169);
    case 'y': return RGB(195,146,58);
    case 'Y': return RGB(249,215,123);
    case 'G': return RGB(116,177,85);
    case 'g': return RGB(55,107,52);
    case 'R': return RGB(207,98,76);
    case 'r': return RGB(139,65,55);
    default: return RGB(255,0,255);
    }
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
            SetDCBrushColor(dc,color(glyphs[i][y][x]));
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

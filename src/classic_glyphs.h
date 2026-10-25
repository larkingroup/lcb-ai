#ifndef LCB_CLASSIC_GLYPHS_H
#define LCB_CLASSIC_GLYPHS_H
/* Shared by the Windows and Linux interfaces so both draw the same icons. */
enum { ClassicDocument, ClassicFolder, ClassicRun, ClassicStop,
    ClassicEngine, ClassicModel, ClassicOutput, ClassicSettings, ClassicIconCount };

/* Only the files that draw the icons define this before including. */
#ifdef LCB_CLASSIC_GLYPH_DATA
/* Hand-drawn 16px glyphs. Dot pixels become the image-list transparency mask. */
static const char classic_glyphs[ClassicIconCount][16][17] = {
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

/* Palette for the glyph letters; returns 0 for a transparent pixel. */
static inline unsigned long classic_glyph_rgb(char c)
{
    switch(c) {
    case 'k': return 0x4a535bUL;
    case 'w': return 0xfffff5UL;
    case 'b': return 0x94b3c9UL;
    case 'B': return 0x4175a9UL;
    case 'y': return 0xc3923aUL;
    case 'Y': return 0xf9d77bUL;
    case 'G': return 0x74b155UL;
    case 'g': return 0x376b34UL;
    case 'R': return 0xcf624cUL;
    case 'r': return 0x8b4137UL;
    default: return 0;
    }
}
#endif
#endif

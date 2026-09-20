#include "markdown.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wchar_t output[131072];
static unsigned styles[131072];
static size_t used;
static void collect(void *context,const wchar_t *text,unsigned style)
{
    size_t n=wcslen(text),i;
    (void)context;
    assert(used+n<sizeof(output)/sizeof(output[0]));
    for(i=0;i<n;i++) { output[used]=text[i]; styles[used++]=style; }
    output[used]=0;
}
static void render(const wchar_t *text,const wchar_t *expected)
{
    used=0; markdown_render(text,collect,NULL);
    if(wcscmp(output,expected)) { fwprintf(stderr,L"Expected: %ls\nActual: %ls\n",expected,output); abort(); }
}
static unsigned styleof(const wchar_t *text)
{
    wchar_t *found=wcsstr(output,text); assert(found); return styles[found-output];
}
int main(void)
{
    wchar_t *longtext=calloc(65537,sizeof(*longtext));
    size_t i;
    assert(longtext);
    render(L"# Heading\n\n* **Bold** and *italic* with `a*b`\n> Quote\n1. Item",
        L"Heading\r\n\r\n  \x2022 Bold and italic with a*b\r\n  |  Quote\r\n1. Item");
    assert(styleof(L"Heading")== (MdBold|MdHeading));
    assert(styleof(L"Bold")==MdBold && styleof(L"italic")==MdItalic);
    assert(styleof(L"a*b")==MdCode && styleof(L"Quote")==MdQuote);
    assert(styleof(L"1. Item")==0);
    render(L"***both*** **bold *nested* text** plain",L"both bold nested text plain");
    assert(styleof(L"both")== (MdBold|MdItalic));
    assert(styleof(L"nested")== (MdBold|MdItalic));
    assert(styleof(L"plain")==0);
    render(L"```c\r\n  **literal** <tag>\\path\r\n```\r\nplain",
        L"  **literal** <tag>\\path\r\nplain");
    assert(styleof(L"**literal**")==MdCode && styleof(L"plain")==0);
    render(L"~~~\n**unfinished\n",L"**unfinished\r\n");
    assert(styleof(L"**unfinished")==MdCode);
    render(L"[site](https://example.test) \\*literal\\* file_name_here",
        L"site (https://example.test) *literal* file_name_here");
    render(L"**unfinished and `unfinished [broken](x",L"**unfinished and `unfinished [broken](x");
    render(L"caf\xe9 \x03a9 <b>plain</b> \xd83d\xde80",L"caf\xe9 \x03a9 <b>plain</b> \xd83d\xde80");
    for(i=0;i<65536;i++) longtext[i]=i%2?L' ':L'[';
    render(longtext,longtext);
    for(i=0;i<65536;i++) longtext[i]=L'x';
    longtext[510]=0xd83d; longtext[511]=0xde80;
    render(longtext,longtext);
    free(longtext); puts("Markdown tests passed."); return 0;
}

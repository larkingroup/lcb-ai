#ifndef LCB_MARKDOWN_H
#define LCB_MARKDOWN_H
#include <stddef.h>
#include <wchar.h>
enum { MdBold=1, MdItalic=2, MdCode=4, MdHeading=8, MdQuote=16 };
/* Text spans are NUL terminated; source text is never modified. */
typedef void (*MarkdownEmit)(void *context, const wchar_t *text, unsigned style);
void markdown_render(const wchar_t *text, MarkdownEmit emit, void *context);
#endif

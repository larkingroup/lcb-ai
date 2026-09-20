#include "markdown.h"
#include <wctype.h>

typedef struct Writer {
    MarkdownEmit emit;
    void *context;
    wchar_t text[513];
    size_t count, budget;
    unsigned style;
} Writer;

static void flush(Writer *w)
{
    if(w->count) { w->text[w->count]=0; w->emit(w->context,w->text,w->style); w->count=0; }
}
static void put(Writer *w,const wchar_t *s,size_t n,unsigned style)
{
    size_t i;
    if(style!=w->style) { flush(w); w->style=style; }
    for(i=0;i<n;i++) {
        w->text[w->count++]=s[i];
        if(w->count==512 || (w->count>=511 && !(s[i]>=0xd800 && s[i]<=0xdbff))) flush(w);
    }
}
static size_t closing(Writer *w,const wchar_t *s,size_t begin,size_t end,wchar_t mark,size_t count)
{
    size_t i,j;
    for(i=begin;i+count<=end && w->budget;i++) {
        w->budget--;
        if(mark!=L'`' && s[i]==L'\\') { i++; continue; }
        if(mark!=L'`' && iswspace(s[i-1])) continue;
        for(j=0;j<count && s[i+j]==mark;j++) {}
        if(j==count && (i+count==end || s[i+count]!=mark)) return i;
    }
    return end;
}
static void inlines(Writer *w,const wchar_t *s,size_t n,unsigned style,unsigned depth)
{
    size_t i=0,run=0,j,k,count;
    if(depth>=8) { put(w,s,n,style); return; }
    while(i<n) {
        if(s[i]==L'\\' && i+1<n && wcschr(L"\\`*_{}[]()#+-.!>",s[i+1])) {
            put(w,s+run,i-run,style); put(w,s+i+1,1,style); i+=2; run=i; continue;
        }
        if(s[i]==L'`' || s[i]==L'*' || s[i]==L'_') {
            wchar_t mark=s[i];
            count=1;
            while(i+count<n && s[i+count]==mark) count++;
            if(mark!=L'`' && (count>3 || (mark==L'_' && i && iswalnum(s[i-1])))) { i+=count; continue; }
            if(i+count<n && (mark==L'`' || !iswspace(s[i+count]))) {
                j=closing(w,s,i+count,n,mark,count);
                if(j<n && j>i+count) {
                    unsigned next=style;
                    put(w,s+run,i-run,style);
                    if(mark==L'`') put(w,s+i+count,j-i-count,style|MdCode);
                    else {
                        next|=count==3?MdBold|MdItalic:count==2?MdBold:MdItalic;
                        inlines(w,s+i+count,j-i-count,next,depth+1);
                    }
                    i=j+count; run=i; continue;
                }
            }
            i+=count; continue;
        }
        if(s[i]==L'[') {
            for(j=i+1;j<n && w->budget && s[j]!=L']';j++) w->budget--;
            if(j+1<n && s[j]==L']' && s[j+1]==L'(') {
                for(k=j+2;k<n && w->budget && s[k]!=L')';k++) w->budget--;
                if(k<n && s[k]==L')' && k>j+2) {
                    put(w,s+run,i-run,style); inlines(w,s+i+1,j-i-1,style,depth+1);
                    put(w,L" (",2,style); put(w,s+j+2,k-j-2,style); put(w,L")",1,style);
                    i=k+1; run=i; continue;
                }
            }
        }
        i++;
    }
    put(w,s+run,n-run,style);
}

void markdown_render(const wchar_t *text,MarkdownEmit emit,void *context)
{
    Writer w={0};
    const wchar_t *s=text,*end;
    wchar_t fence=0;
    size_t fencecount=0;
    w.emit=emit; w.context=context; w.budget=wcslen(text)*16;
    while(*s) {
        size_t n,offset=0,k,count;
        unsigned style=0;
        int newline;
        end=wcschr(s,L'\n'); n=end?(size_t)(end-s):wcslen(s); newline=end!=NULL;
        if(n && s[n-1]==L'\r') n--;
        while(offset<n && offset<3 && s[offset]==L' ') offset++;
        k=offset;
        if(k<n && (s[k]==L'`' || s[k]==L'~')) {
            wchar_t mark=s[k];
            while(k<n && s[k]==mark) k++;
            count=k-offset;
            if(count>=3 && (!fence || (mark==fence && count>=fencecount))) {
                size_t tail=k;
                while(tail<n && iswspace(s[tail])) tail++;
                if(!fence || tail==n) {
                    if(fence) fence=0;
                    else { fence=mark; fencecount=count; }
                    s=end?end+1:s+wcslen(s); continue;
                }
            }
        }
        if(fence) put(&w,s,n,MdCode);
        else {
            k=offset;
            while(k<n && s[k]==L'#') k++;
            if(k>offset && k-offset<=6 && k<n && s[k]==L' ') { offset=k+1; style=MdBold|MdHeading; }
            else if(offset<n && s[offset]==L'>') {
                offset++; if(offset<n && s[offset]==L' ') offset++;
                style=MdQuote; put(&w,L"  |  ",5,style);
            } else if(offset+1<n && wcschr(L"*-+",s[offset]) && s[offset+1]==L' ') {
                put(&w,L"  \x2022 ",4,style); offset+=2;
            } else offset=0;
            inlines(&w,s+offset,n-offset,style,0);
        }
        if(newline) put(&w,L"\r\n",2,fence?MdCode:0);
        s=end?end+1:s+wcslen(s);
    }
    flush(&w);
}

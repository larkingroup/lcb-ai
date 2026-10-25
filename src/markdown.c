#include "markdown.h"
#include <wctype.h>

typedef struct Writer {
    MarkdownEmit emit;
    void *context;
    wchar_t text[513];
    size_t count, budget, columns;
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

enum { TableColumns=16, TableCell=256, TableWidth=40 };
typedef struct Capture { wchar_t text[TableCell+1]; size_t count; } Capture;
static void capture(void *context,const wchar_t *text,unsigned style)
{
    Capture *c=context;
    (void)style;
    while(*text && c->count<TableCell) c->text[c->count++]=*text++;
    c->text[c->count]=0;
}
static size_t line_length(const wchar_t *s)
{
    const wchar_t *end=wcschr(s,L'\n');
    size_t n=end?(size_t)(end-s):wcslen(s);
    if(n && s[n-1]==L'\r') n--;
    return n;
}
static const wchar_t *next_line(const wchar_t *s)
{
    const wchar_t *end=wcschr(s,L'\n');
    return end?end+1:s+wcslen(s);
}
/* Splits "| a | b |" into trimmed cells; returns 0 when the line is not a row. */
static int table_cells(const wchar_t *s,size_t n,size_t *start,size_t *length)
{
    size_t i=0,from;
    int count=0,code=0;
    while(i<n && s[i]==L' ') i++;
    if(i>=n || s[i]!=L'|') return 0;
    from=++i;
    for(;i<=n;i++) {
        if(i<n && s[i]==L'\\' && i+1<n) { i++; continue; }
        if(i<n && s[i]==L'`') code=!code;
        if(i==n || (s[i]==L'|' && !code)) {
            size_t a=from,b=i;
            while(a<b && iswspace(s[a])) a++;
            while(b>a && iswspace(s[b-1])) b--;
            if(i==n && a==b) break; /* Trailing pipe. */
            if(count==TableColumns) return 0;
            start[count]=a; length[count++]=b-a; from=i+1;
        }
    }
    return count;
}
static int table_rule(const wchar_t *s,size_t n,int columns)
{
    size_t start[TableColumns],length[TableColumns],i;
    int count=table_cells(s,n,start,length),c;
    if(count!=columns) return 0;
    for(c=0;c<count;c++) {
        int dashes=0;
        for(i=start[c];i<start[c]+length[c];i++) {
            if(s[i]==L'-') dashes++;
            else if(s[i]!=L':') return 0;
        }
        if(!dashes) return 0;
    }
    return 1;
}
static void cell_text(const wchar_t *s,size_t n,Capture *out)
{
    Writer w={0};
    out->count=0; out->text[0]=0;
    w.emit=capture; w.context=out; w.budget=n*16+16;
    inlines(&w,s,n,0,0); flush(&w);
}
/* Next piece of a cell that fits width, broken at a space when possible. */
static size_t cell_piece(const Capture *cell,size_t from,size_t width)
{
    size_t n=cell->count-from,k;
    if(n<=width) return n;
    for(k=width;k>width/2;k--) if(cell->text[from+k]==L' ') return k;
    return width;
}
/* Tables become monospace grids so columns line up in any renderer. */
static const wchar_t *table(Writer *w,const wchar_t *s,int columns)
{
    size_t start[TableColumns],length[TableColumns],width[TableColumns]={0},k;
    size_t at[TableColumns],used[TableColumns],total=0,room;
    const wchar_t *row,*end=s;
    Capture cells[TableColumns];
    int c,count,line,more;
    for(row=s,line=0;*row;row=next_line(row),line++) {
        if(line==1) continue;
        count=table_cells(row,line_length(row),start,length);
        if(!count) break;
        for(c=0;c<columns && c<count;c++) {
            cell_text(row+start[c],length[c],&cells[0]);
            if(cells[0].count>width[c]) width[c]=cells[0].count<TableWidth?cells[0].count:TableWidth;
        }
    }
    for(c=0;c<columns;c++) { if(!width[c]) width[c]=1; total+=width[c]; }
    /* Narrow the widest columns until the grid fits; cells then wrap. */
    room=w->columns>(size_t)(3*columns+2) ? w->columns-3*columns-2 : 0;
    while(w->columns && total>room) {
        int widest=0;
        for(c=1;c<columns;c++) if(width[c]>width[widest]) widest=c;
        if(width[widest]<=6) break;
        width[widest]--; total--;
    }
    put(w,L" \r\n",3,MdCode|MdBlock);
    for(row=s,line=0;*row;row=next_line(row),line++) {
        if(line==1) {
            put(w,L" ",1,MdCode|MdBlock);
            for(c=0;c<columns;c++) {
                for(k=0;k<width[c];k++) put(w,L"-",1,MdCode|MdBlock);
                if(c+1<columns) put(w,L"-+-",3,MdCode|MdBlock);
            }
            put(w,L"\r\n",2,MdCode|MdBlock);
            continue;
        }
        count=table_cells(row,line_length(row),start,length);
        if(!count) break;
        for(c=0;c<columns;c++) {
            cells[c].count=0; cells[c].text[0]=0; at[c]=0;
            if(c<count) cell_text(row+start[c],length[c],&cells[c]);
        }
        do {
            more=0;
            put(w,L" ",1,MdCode|MdBlock);
            for(c=0;c<columns;c++) {
                used[c]=cell_piece(&cells[c],at[c],width[c]);
                put(w,cells[c].text+at[c],used[c],MdCode|MdBlock|(line?0:MdBold));
                for(k=used[c];k<width[c];k++) put(w,L" ",1,MdCode|MdBlock);
                if(c+1<columns) put(w,L" | ",3,MdCode|MdBlock);
                at[c]+=used[c];
                while(at[c]<cells[c].count && cells[c].text[at[c]]==L' ') at[c]++;
                more|=at[c]<cells[c].count;
            }
            put(w,L"\r\n",2,MdCode|MdBlock);
        } while(more);
        end=next_line(row);
    }
    put(w,L" \r\n",3,MdCode|MdBlock);
    return end;
}

void markdown_render(const wchar_t *text,MarkdownEmit emit,void *context)
{
    markdown_render_width(text,0,emit,context);
}

void markdown_render_width(const wchar_t *text,size_t columns,MarkdownEmit emit,void *context)
{
    Writer w={0};
    w.columns=columns;
    const wchar_t *s=text,*end;
    wchar_t fence=0;
    size_t fencecount=0;
    w.emit=emit; w.context=context; w.budget=wcslen(text)*16;
    while(*s) {
        size_t n,offset=0,k,count,starts[TableColumns],lengths[TableColumns];
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
                    put(&w,L" \r\n",3,MdCode|MdBlock);
                    s=end?end+1:s+wcslen(s); continue;
                }
            }
        }
        if(fence) put(&w,n?s:L" ",n?n:1,MdCode|MdBlock);
        else if(end && (count=(size_t)table_cells(s,n,starts,lengths))>0 &&
            table_rule(end+1,line_length(end+1),(int)count)) {
            s=table(&w,s,(int)count); continue;
        } else {
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
        if(newline) put(&w,L"\r\n",2,fence?MdCode|MdBlock:0);
        s=end?end+1:s+wcslen(s);
    }
    flush(&w);
}

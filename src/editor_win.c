#include "editor_win.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include <wchar.h>

typedef struct Editor Editor;
struct Editor {
	HWND name, prompt;
	wchar_t *outname, *outprompt;
	size_t namecap, promptcap;
	int done, ok;
	HBRUSH face;
};

static LRESULT CALLBACK
editorproc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
	Editor *e=(Editor *)GetWindowLongPtrW(window,GWLP_USERDATA);
	if(message==WM_NCCREATE) {
		e=(Editor *)((CREATESTRUCTW *)lp)->lpCreateParams;
		SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)e);
	}
	if(!e) return DefWindowProcW(window,message,wp,lp);
	if(message==WM_CTLCOLORSTATIC) {
		SetTextColor((HDC)wp,RGB(47,55,58)); SetBkColor((HDC)wp,RGB(223,220,207));
		return (LRESULT)e->face;
	}
	if(message==WM_COMMAND && LOWORD(wp)==IDOK) {
		wchar_t name[481], *p;
		GetWindowTextW(e->name,name,481);
		for(p=name;*p==L' ' || *p==L'\t';p++) {}
		if(!*p) { MessageBoxW(window,L"Enter a workspace name.",L"lts-ai",MB_OK); return 0; }
		GetWindowTextW(e->name,e->outname,(int)e->namecap);
		GetWindowTextW(e->prompt,e->outprompt,(int)e->promptcap);
		e->ok=1; e->done=1; return 0;
	}
	if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wp)==IDCANCEL)) { e->done=1; return 0; }
	return DefWindowProcW(window,message,wp,lp);
}

static HWND
child(HWND parent, HFONT font, const wchar_t *kind, const wchar_t *text,
    DWORD style, int id, int x, int y, int w, int h, int dpi)
{
	HWND c=CreateWindowW(kind,text,WS_CHILD|WS_VISIBLE|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),
	    MulDiv(w,dpi,96),MulDiv(h,dpi,96),parent,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
	SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE); SetWindowTheme(c,L"",L""); return c;
}

int
edit_workspace(HWND owner, HFONT font, const wchar_t *title,
    wchar_t *name, size_t namecap, wchar_t *prompt, size_t promptcap)
{
	WNDCLASSW wc={0};
	Editor e={0};
	HWND window;
	RECT r, bounds={0,0,560,390};
	MSG msg;
	HDC dc=GetDC(owner);
	int dpi=GetDeviceCaps(dc,LOGPIXELSY), result;
	HBRUSH brush=CreateSolidBrush(RGB(223,220,207));
	DWORD corner=1, caption=RGB(223,220,207);
	ReleaseDC(owner,dc);
	wc.lpfnWndProc=editorproc; wc.hInstance=GetModuleHandleW(NULL);
	wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hbrBackground=brush; wc.lpszClassName=L"LTSWorkspaceEditor";
	if(!RegisterClassW(&wc)) { DeleteObject(brush); return 0; }
	e.outname=name; e.namecap=namecap; e.outprompt=prompt; e.promptcap=promptcap; e.face=brush;
	bounds.right=MulDiv(bounds.right,dpi,96); bounds.bottom=MulDiv(bounds.bottom,dpi,96);
	AdjustWindowRect(&bounds,WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,FALSE);
	GetWindowRect(owner,&r);
	window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,title,WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,
	    r.left+(r.right-r.left-(bounds.right-bounds.left))/2,r.top+(r.bottom-r.top-(bounds.bottom-bounds.top))/2,
	    bounds.right-bounds.left,bounds.bottom-bounds.top,owner,NULL,wc.hInstance,&e);
	if(!window) { UnregisterClassW(wc.lpszClassName,wc.hInstance); DeleteObject(brush); return 0; }
	DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
	DwmSetWindowAttribute(window,35,&caption,sizeof(caption));
	child(window,font,L"STATIC",L"Name",0,0,16,14,520,20,dpi);
	e.name=child(window,font,L"EDIT",name,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,10,16,37,528,25,dpi);
	SendMessageW(e.name,EM_SETLIMITTEXT,(WPARAM)namecap-1,0);
	child(window,font,L"STATIC",L"Master prompt",0,0,16,77,520,20,dpi);
	e.prompt=child(window,font,L"EDIT",prompt,WS_BORDER|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,11,16,100,528,215,dpi);
	SendMessageW(e.prompt,EM_SETLIMITTEXT,(WPARAM)promptcap-1,0);
	child(window,font,L"STATIC",L"Used for the next message in every chat in this workspace.",0,0,16,323,528,20,dpi);
	child(window,font,L"BUTTON",L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,362,354,86,25,dpi);
	child(window,font,L"BUTTON",L"Cancel",WS_TABSTOP,IDCANCEL,458,354,86,25,dpi);
	EnableWindow(owner,FALSE); ShowWindow(window,SW_SHOW); SetFocus(e.name);
	while(!e.done && (result=GetMessageW(&msg,NULL,0,0))>0) {
		if(!IsDialogMessageW(window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	}
	EnableWindow(owner,TRUE); DestroyWindow(window); SetActiveWindow(owner);
	UnregisterClassW(wc.lpszClassName,wc.hInstance); DeleteObject(brush);
	return e.ok;
}

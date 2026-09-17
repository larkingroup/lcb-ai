#include "setup_win.h"
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wchar.h>

enum { EnginePath=10, ModelPath, BrowseEngine, BrowseModel, DownloadEngine, OtherEngines, DownloadModel };
typedef struct Setup {
	HWND enginefield, modelfield, status, finish;
	wchar_t engine[MAX_PATH], model[MAX_PATH];
	HBRUSH face;
	int done, saved;
} Setup;

static int localfile(const wchar_t *path, const wchar_t *extension)
{
	DWORD attrs;
	const wchar_t *suffix=wcsrchr(path,L'.');
	if(!suffix || _wcsicmp(suffix,extension)) return 0;
	attrs=GetFileAttributesW(path);
	return attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY);
}

int setup_paths_ready(const wchar_t *engine, const wchar_t *model)
{
	return localfile(engine,L".exe") && localfile(model,L".gguf");
}

static void refresh(Setup *s)
{
	int engine=localfile(s->engine,L".exe"),model=localfile(s->model,L".gguf");
	SetWindowTextW(s->enginefield,engine?s->engine:L"");
	SetWindowTextW(s->modelfield,model?s->model:L"");
	SetWindowTextW(s->status,engine && model?L"Both files are selected. Save setup, then choose Load in the toolbar.":
	    !engine?L"Choose llama-server.exe from the extracted engine folder.":L"Choose a local .gguf model file to finish setup.");
	EnableWindow(s->finish,engine && model);
}

static void browse(HWND window, Setup *s, int engine)
{
	OPENFILENAMEW of={0};
	wchar_t path[MAX_PATH]={0};
	const wchar_t *current=engine?s->engine:s->model;
	if(localfile(current,engine?L".exe":L".gguf")) wcscpy(path,current);
	of.lStructSize=sizeof(of); of.hwndOwner=window;
	of.lpstrFile=path; of.nMaxFile=MAX_PATH;
	of.lpstrTitle=engine?L"Choose the extracted llama-server.exe":L"Choose an existing GGUF model";
	of.lpstrFilter=engine?L"llama.cpp server (llama-server.exe)\0llama-server.exe\0Executable files (*.exe)\0*.exe\0\0":
	    L"GGUF model (*.gguf)\0*.gguf\0\0";
	of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
	if(GetOpenFileNameW(&of)) { wcscpy(engine?s->engine:s->model,path); refresh(s); }
}

static void openlink(HWND window, const wchar_t *url)
{
	if((INT_PTR)ShellExecuteW(window,L"open",url,NULL,NULL,SW_SHOWNORMAL)<=32)
		MessageBoxW(window,L"Windows could not open your browser. Check the default browser and try again.",L"LTS AI Setup",MB_OK|MB_ICONINFORMATION);
}

static LRESULT CALLBACK setupproc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
	Setup *s=(Setup *)GetWindowLongPtrW(window,GWLP_USERDATA);
	if(message==WM_NCCREATE) {
		s=(Setup *)((CREATESTRUCTW *)lp)->lpCreateParams;
		SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)s);
	}
	if(!s) return DefWindowProcW(window,message,wp,lp);
	if(message==WM_CTLCOLORSTATIC) {
		SetTextColor((HDC)wp,RGB(47,55,58)); SetBkColor((HDC)wp,RGB(236,233,216));
		return (LRESULT)s->face;
	}
	if(message==WM_CLOSE) { s->done=1; return 0; }
	if(message==WM_COMMAND) {
		switch(LOWORD(wp)) {
		case BrowseEngine: browse(window,s,1); break;
		case BrowseModel: browse(window,s,0); break;
		case DownloadEngine:
			openlink(window,L"https://github.com/ggml-org/llama.cpp/releases/download/b10566/llama-b10566-bin-win-cpu-x64.zip"); break;
		case OtherEngines: openlink(window,L"https://github.com/ggml-org/llama.cpp/releases"); break;
		case DownloadModel:
			openlink(window,L"https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF/resolve/main/SmolLM2-135M-Instruct-Q4_K_M.gguf?download=true"); break;
		case IDOK:
			if(setup_paths_ready(s->engine,s->model)) { s->saved=1; s->done=1; }
			else refresh(s);
			break;
		case IDCANCEL: s->done=1; break;
		}
		return 0;
	}
	return DefWindowProcW(window,message,wp,lp);
}

static HWND child(HWND parent, HFONT font, int dpi, const wchar_t *kind, const wchar_t *text,
    DWORD style, int id, int x, int y, int w, int h)
{
	HWND c=CreateWindowW(kind,text,WS_CHILD|WS_VISIBLE|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),
	    MulDiv(w,dpi,96),MulDiv(h,dpi,96),parent,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
	SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE); SetWindowTheme(c,L"",L""); return c;
}

int setup_dialog(HWND owner, HFONT font, wchar_t engine[MAX_PATH], wchar_t model[MAX_PATH])
{
	WNDCLASSW wc={0};
	Setup s={0};
	HWND window;
	RECT r,bounds={0,0,600,454};
	MSG msg={0};
	HDC dc=GetDC(owner);
	int dpi=GetDeviceCaps(dc,LOGPIXELSY),result=1;
	DWORD corner=1;
	ReleaseDC(owner,dc);
	wcscpy(s.engine,engine); wcscpy(s.model,model);
	s.face=CreateSolidBrush(RGB(236,233,216));
	wc.lpfnWndProc=setupproc; wc.hInstance=GetModuleHandleW(NULL);
	wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hbrBackground=s.face; wc.lpszClassName=L"LTSSetup";
	if(!RegisterClassW(&wc)) { DeleteObject(s.face); return 0; }
	bounds.right=MulDiv(bounds.right,dpi,96); bounds.bottom=MulDiv(bounds.bottom,dpi,96);
	AdjustWindowRect(&bounds,WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,FALSE);
	GetWindowRect(owner,&r);
	window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"LTS AI Setup",WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,
	    r.left+(r.right-r.left-(bounds.right-bounds.left))/2,r.top+(r.bottom-r.top-(bounds.bottom-bounds.top))/2,
	    bounds.right-bounds.left,bounds.bottom-bounds.top,owner,NULL,wc.hInstance,&s);
	if(!window) { UnregisterClassW(wc.lpszClassName,wc.hInstance); DeleteObject(s.face); return 0; }
	DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
	child(window,font,dpi,L"STATIC",L"Welcome to LTS AI",0,0,18,16,564,20);
	child(window,font,dpi,L"STATIC",L"Choose an engine and a model already on this computer, or get them below.",0,0,18,43,564,22);
	child(window,font,dpi,L"BUTTON",L"1. Local engine",BS_GROUPBOX,0,16,76,568,146);
	child(window,font,dpi,L"STATIC",L"Recommended: llama.cpp for Windows x64 (CPU). Download: about 19 MB.",0,0,30,99,540,20);
	child(window,font,dpi,L"BUTTON",L"Download CPU engine...",WS_TABSTOP,DownloadEngine,30,124,174,25);
	child(window,font,dpi,L"BUTTON",L"Other builds...",WS_TABSTOP,OtherEngines,213,124,116,25);
	child(window,font,dpi,L"STATIC",L"Extract the ZIP. Keep its DLL files together, then choose llama-server.exe.",0,0,30,157,540,20);
	s.enginefield=child(window,font,dpi,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,EnginePath,30,184,446,23);
	child(window,font,dpi,L"BUTTON",L"Browse...",WS_TABSTOP,BrowseEngine,486,183,82,25);
	child(window,font,dpi,L"BUTTON",L"2. Local model",BS_GROUPBOX,0,16,232,568,117);
	child(window,font,dpi,L"STATIC",L"Use a .gguf file. SmolLM2 135M is a tiny starter model (105 MB).",0,0,30,255,540,20);
	child(window,font,dpi,L"BUTTON",L"Download tiny model...",WS_TABSTOP,DownloadModel,30,279,174,25);
	s.modelfield=child(window,font,dpi,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,ModelPath,30,313,446,23);
	child(window,font,dpi,L"BUTTON",L"Browse...",WS_TABSTOP,BrowseModel,486,312,82,25);
	s.status=child(window,font,dpi,L"STATIC",L"",0,0,18,360,564,34);
	child(window,font,dpi,L"STATIC",L"Downloads open in your browser. Revisit this screen from Settings > Setup.",0,0,18,394,564,20);
	s.finish=child(window,font,dpi,L"BUTTON",L"Save setup",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,374,423,100,25);
	child(window,font,dpi,L"BUTTON",L"Later",WS_TABSTOP,IDCANCEL,484,423,100,25);
	refresh(&s);
	EnableWindow(owner,FALSE); ShowWindow(window,SW_SHOW);
	SetFocus(GetDlgItem(window,setup_paths_ready(s.engine,s.model)?IDOK:
	    localfile(s.engine,L".exe")?BrowseModel:BrowseEngine));
	while(!s.done && (result=GetMessageW(&msg,NULL,0,0))>0) {
		if(!IsDialogMessageW(window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	}
	EnableWindow(owner,TRUE); DestroyWindow(window); SetActiveWindow(owner);
	UnregisterClassW(wc.lpszClassName,wc.hInstance); DeleteObject(s.face);
	if(result==0) PostQuitMessage((int)msg.wParam);
	if(s.saved) { wcscpy(engine,s.engine); wcscpy(model,s.model); }
	return s.saved;
}

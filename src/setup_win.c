#include "setup_win.h"
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wchar.h>

enum { EnginePath=10, ModelPath, BrowseEngine, BrowseModel, DownloadEngine, OtherEngines, DownloadModel,
    ChooseLlama, ChooseOllama, ChooseKobold, DownloadOllama, DownloadKobold };
typedef struct Setup {
	HWND enginefield, modelfield, enginenote, status, finish, choices[3];
	wchar_t engine[MAX_PATH], model[MAX_PATH];
	HBRUSH face;
	int done, saved, selected;
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
	int i;
	for(i=0;i<3;i++) SendMessageW(s->choices[i],BM_SETCHECK,i==s->selected?BST_CHECKED:BST_UNCHECKED,0);
	SetWindowTextW(s->enginenote,s->selected==0?
	    L"For lcb-ai: extract the llama.cpp ZIP, keep its DLLs together, then choose llama-server.exe.\r\nCPU is the easiest starting point. Other builds include GPU acceleration.":
	    s->selected==1?L"Ollama is a separate runner with its own model library and service.\r\nlcb-ai currently connects to llama.cpp; select llama.cpp to finish setup here.":
	    L"KoboldCpp is a separate GGUF runner with its own web interface.\r\nlcb-ai currently connects to llama.cpp; select llama.cpp to finish setup here.");
	EnableWindow(s->enginefield,s->selected==0);
	EnableWindow(GetDlgItem(GetParent(s->enginefield),BrowseEngine),s->selected==0);
	SetWindowTextW(s->enginefield,engine?s->engine:L"");
	SetWindowTextW(s->modelfield,model?s->model:L"");
	SetWindowTextW(s->status,s->selected!=0?L"Alternative engine downloads open in your browser. Choose llama.cpp to use your models in lcb-ai.":
	    engine && model?L"Both files are selected. Save setup, then choose Load in the toolbar.":
	    !engine?L"Choose llama-server.exe from the extracted engine folder.":L"Choose a local .gguf model file to finish setup.");
	EnableWindow(s->finish,s->selected==0 && engine && model);
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
		MessageBoxW(window,L"Windows could not open your browser. Check the default browser and try again.",L"lcb-ai Setup",MB_OK|MB_ICONINFORMATION);
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
		case ChooseLlama: case ChooseOllama: case ChooseKobold:
			s->selected=LOWORD(wp)-ChooseLlama; refresh(s); break;
		case BrowseEngine: if(s->selected==0) browse(window,s,1); break;
		case BrowseModel: browse(window,s,0); break;
		case DownloadEngine:
			openlink(window,L"https://github.com/ggml-org/llama.cpp/releases/download/b10566/llama-b10566-bin-win-cpu-x64.zip"); break;
		case OtherEngines: openlink(window,L"https://github.com/ggml-org/llama.cpp/releases"); break;
		case DownloadOllama: openlink(window,L"https://ollama.com/download/windows"); break;
		case DownloadKobold: openlink(window,L"https://github.com/LostRuins/koboldcpp/releases/latest"); break;
		case DownloadModel:
			openlink(window,L"https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF/resolve/main/SmolLM2-135M-Instruct-Q4_K_M.gguf?download=true"); break;
		case IDOK:
			if(s->selected==0 && setup_paths_ready(s->engine,s->model)) { s->saved=1; s->done=1; }
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
	RECT r,bounds={0,0,780,604};
	MSG msg={0};
	HDC dc=GetDC(owner);
	int dpi=GetDeviceCaps(dc,LOGPIXELSY),result=1;
	DWORD corner=1;
	ReleaseDC(owner,dc);
	wcscpy(s.engine,engine); wcscpy(s.model,model);
	s.face=CreateSolidBrush(RGB(236,233,216));
	wc.lpfnWndProc=setupproc; wc.hInstance=GetModuleHandleW(NULL);
	wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hbrBackground=s.face; wc.lpszClassName=L"LCBSetup";
	if(!RegisterClassW(&wc)) { DeleteObject(s.face); return 0; }
	bounds.right=MulDiv(bounds.right,dpi,96); bounds.bottom=MulDiv(bounds.bottom,dpi,96);
	AdjustWindowRect(&bounds,WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,FALSE);
	GetWindowRect(owner,&r);
	window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"lcb-ai Setup",WS_CAPTION|WS_SYSMENU|WS_DLGFRAME,
	    r.left+(r.right-r.left-(bounds.right-bounds.left))/2,r.top+(r.bottom-r.top-(bounds.bottom-bounds.top))/2,
	    bounds.right-bounds.left,bounds.bottom-bounds.top,owner,NULL,wc.hInstance,&s);
	if(!window) { UnregisterClassW(wc.lpszClassName,wc.hInstance); DeleteObject(s.face); return 0; }
	DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
	child(window,font,dpi,L"STATIC",L"Welcome to lcb-ai",0,0,18,16,744,20);
	child(window,font,dpi,L"STATIC",L"An engine runs your models. Choose one below; llama.cpp is the default for lcb-ai.",0,0,18,43,744,22);
	child(window,font,dpi,L"BUTTON",L"1. Choose a local engine",BS_GROUPBOX,0,16,76,748,294);
	child(window,font,dpi,L"BUTTON",L"",BS_GROUPBOX,0,30,100,236,166);
	s.choices[0]=child(window,font,dpi,L"BUTTON",L"llama.cpp (default)",WS_TABSTOP|BS_RADIOBUTTON,ChooseLlama,42,114,212,23);
	child(window,font,dpi,L"STATIC",L"Runs GGUF files on CPU or GPU.\r\nGood for direct model loading.\r\nWorks with lcb-ai.\r\nCPU download: about 19 MB.",0,0,42,145,212,72);
	child(window,font,dpi,L"BUTTON",L"Download llama.cpp",WS_TABSTOP,DownloadEngine,42,230,212,25);
	child(window,font,dpi,L"BUTTON",L"",BS_GROUPBOX,0,272,100,236,166);
	s.choices[1]=child(window,font,dpi,L"BUTTON",L"Ollama",WS_TABSTOP|BS_RADIOBUTTON,ChooseOllama,284,114,212,23);
	child(window,font,dpi,L"STATIC",L"Model library and local API.\r\nGood for managing models\r\nwith simple commands.\r\nRuns separately from lcb-ai.",0,0,284,145,212,72);
	child(window,font,dpi,L"BUTTON",L"Get Ollama...",WS_TABSTOP,DownloadOllama,284,230,212,25);
	child(window,font,dpi,L"BUTTON",L"",BS_GROUPBOX,0,514,100,236,166);
	s.choices[2]=child(window,font,dpi,L"BUTTON",L"KoboldCpp",WS_TABSTOP|BS_RADIOBUTTON,ChooseKobold,526,114,212,23);
	child(window,font,dpi,L"STATIC",L"Portable GGUF runner and web UI.\r\nGood for creative writing\r\nand roleplay.\r\nRuns separately from lcb-ai.",0,0,526,145,212,72);
	child(window,font,dpi,L"BUTTON",L"Get KoboldCpp...",WS_TABSTOP,DownloadKobold,526,230,212,25);
	s.enginenote=child(window,font,dpi,L"STATIC",L"",0,0,30,278,540,42);
	child(window,font,dpi,L"BUTTON",L"Other llama.cpp builds...",WS_TABSTOP,OtherEngines,580,284,172,25);
	s.enginefield=child(window,font,dpi,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,EnginePath,30,333,630,23);
	child(window,font,dpi,L"BUTTON",L"Browse...",WS_TABSTOP,BrowseEngine,670,332,82,25);
	child(window,font,dpi,L"BUTTON",L"2. Your local model",BS_GROUPBOX,0,16,380,748,117);
	child(window,font,dpi,L"STATIC",L"Choose your existing .gguf file. Optional: SmolLM2 135M is a tiny setup test (105 MB).",0,0,30,403,722,20);
	child(window,font,dpi,L"BUTTON",L"Download tiny test model...",WS_TABSTOP,DownloadModel,30,427,200,25);
	s.modelfield=child(window,font,dpi,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,ModelPath,30,461,630,23);
	child(window,font,dpi,L"BUTTON",L"Browse...",WS_TABSTOP,BrowseModel,670,460,82,25);
	s.status=child(window,font,dpi,L"STATIC",L"",0,0,18,508,744,34);
	child(window,font,dpi,L"STATIC",L"Downloads open in your browser. Revisit this screen from Settings > Setup.",0,0,18,550,744,20);
	s.finish=child(window,font,dpi,L"BUTTON",L"Save setup",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,554,573,100,25);
	child(window,font,dpi,L"BUTTON",L"Later",WS_TABSTOP,IDCANCEL,664,573,100,25);
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

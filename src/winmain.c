#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <commdlg.h>
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include "lcb.h"
#include "app_paths.h"
#include "engine_win.h"
#include "store_win.h"
#include "editor_win.h"
#include "library_win.h"
#include "layout.h"
#include "classic_win.h"
#include "stream_win.h"
#include "setup_win.h"
#include "model_settings_win.h"
#include "context_win.h"
#include <windowsx.h>

enum { IdModules = 100, IdPrompt, IdSend, IdNew, IdPort, IdExit, IdAbout,
	IdLoad, IdUnload, IdBrowse, IdEngine, IdClear, IdCopy,
	IdWorkspace, IdNewWorkspace, IdEditWorkspace, IdLibrary, IdScan, IdFolder,
	IdRecursive, IdModels, IdChatTabs, IdLibraryTabs, IdDetails,
	IdStop, IdCloseTab, IdGeneration, IdValue, IdViewLeft, IdViewRight, IdViewOutput, IdResetLayout, IdSetup,
	ReplyReady = WM_APP + 1, HealthReady, LibraryReady, StreamReady, BudgetReady };

#define Paper RGB(255,255,255)
#define Face RGB(236,233,216)
#define Light RGB(250,249,241)
#define Shadow RGB(153,154,145)
#define Ink RGB(47,55,58)
#define Muted RGB(103,113,115)
#define Blue RGB(192,211,224)
#define BlueInk RGB(49,76,96)

typedef struct Work Work;
struct Work {
	HWND target;
	char *request;
	char *prompt;
	char *answer;
	char error[256];
	unsigned short port;
	int ok;
	HANDLE cancel;
	const Conversation *conversation;
	char *instruction;
	Generation settings;
	ContextBudget budget;
	ReplyReport report;
	wchar_t modelpath[MAX_PATH];
};

static struct {
	HWND window, modules, transcript, prompt, send, fresh;
	HWND status, port, load, unload, browse, engine, model, activity;
	HWND workspace, newworkspace, editworkspace;
	HWND chattabs, librarytabs, models, folder, recursive, scan, details, master, folderlabel;
	HWND stop, closetab, generation, value, genhelp;
    Generation settings;
    ContextBudget budget;
    HANDLE cancel;
    Work *work;
    int property, treebusy, dragging, leftsize, rightsize, outputsize;
    int answerstart; size_t streamchars;
    int hideleft, hideright, hideoutput;
    char tabs[32][33];
    int ntabs;
    double firstseconds;
    Library *library;
	wchar_t libraryfolder[MAX_PATH];
	int recursivevalue, scanning;
	volatile LONG scancancel;
	HFONT normal, fixed, heading;
	HBRUSH face, paper;
	HIMAGELIST icons;
	HWND hotbutton;
	int activepane;
	HMODULE rich;
	Store store;
	Chat chat;
	size_t selected;
	int dirty, restoring;
	int busy, dpi, width, height, health, checking;
	unsigned epoch;
	unsigned short probeport;
	EngineProcess process;
	wchar_t enginepath[MAX_PATH], modelpath[MAX_PATH], config[MAX_PATH], dataroot[MAX_PATH];
	wchar_t activitytext[8192];
	ULONGLONG started, loadstarted;
	double lastseconds;
} app;

static void layout(void);
static void refreshcontrols(void);
static void refreshtitle(void);
static void pollengine(void);
static int savechat(void);
static void listchats(void);
static void listworkspaces(void);
static void showchat(void);
static void synctabs(void);
static void opentab(void);
static void generationrows(void);
static void loadsettings(void);
static void commitproperty(void);
static void stopreply(void);
static void closetab(void);
static void activatechat(const char *id);
static int runsetup(void);
static const wchar_t *basenamew(const wchar_t *path);
static LRESULT CALLBACK classiccontrol(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

static void
propertyrow(int row, const wchar_t *name, const wchar_t *value)
{
	LVITEMW item={0};
	item.mask=LVIF_TEXT; item.iItem=row; item.pszText=(wchar_t *)name;
	ListView_InsertItem(app.details,&item);
	ListView_SetItemText(app.details,row,1,(wchar_t *)value);
}

static void
modelproperties(const ModelInfo *model)
{
	wchar_t size[40];
	SendMessageW(app.details,WM_SETREDRAW,FALSE,0);
	ListView_DeleteAllItems(app.details);
	if(model) {
		swprintf(size,40,L"%.2f GB",(double)model->bytes/1000000000.0);
		propertyrow(0,L"Name",model->name);
		propertyrow(1,L"Architecture",model->architecture[0]?model->architecture:L"Unspecified");
		propertyrow(2,L"Parameters",model->size[0]?model->size:L"Unspecified");
		propertyrow(3,L"File size",size);
		propertyrow(4,L"Quantization",model_quant(model->filetype));
		propertyrow(5,L"Type",model->projector?L"Projector companion":L"GGUF model");
		propertyrow(6,L"File",basenamew(model->path));
		propertyrow(7,L"Location",model->path);
	} else {
		propertyrow(0,L"Selection",L"No model selected");
		propertyrow(1,L"Inspect",L"Click a model above");
		propertyrow(2,L"Use model",L"Double-click or Enter");
		propertyrow(3,L"Library",L"Choose folder in Library");
	}
	SendMessageW(app.details,WM_SETREDRAW,TRUE,0);
	InvalidateRect(app.details,NULL,TRUE);
}

static void
copyselection(void)
{
	if(GetFocus()==app.details) {
		wchar_t value[512],*destination;
		int row=ListView_GetNextItem(app.details,-1,LVNI_SELECTED);
		SIZE_T bytes;
		HGLOBAL memory;
		if(row<0) return;
		ListView_GetItemText(app.details,row,1,value,512);
		bytes=(wcslen(value)+1)*sizeof(wchar_t);
		memory=GlobalAlloc(GMEM_MOVEABLE,bytes); if(!memory) return;
		destination=GlobalLock(memory);
		if(!destination) { GlobalFree(memory); return; }
		memcpy(destination,value,bytes); GlobalUnlock(memory);
		if(!OpenClipboard(app.window)) { GlobalFree(memory); return; }
		if(!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory);
		CloseClipboard();
	} else SendMessageW(GetFocus(),WM_COPY,0,0);
}

static const wchar_t *
basenamew(const wchar_t *path)
{
	const wchar_t *p = wcsrchr(path, L'\\');
	return p ? p+1 : path;
}

static unsigned short
getport(void)
{
	wchar_t text[16], *end;
	unsigned long port;
	GetWindowTextW(app.port, text, 16);
	port = wcstoul(text, &end, 10);
	return !*end && port > 0 && port <= 65535 ? (unsigned short)port : 0;
}

static void
note(const wchar_t *text)
{
	SYSTEMTIME t;
	wchar_t line[768];
	GetLocalTime(&t);
	swprintf(line, 768, L"%02u:%02u:%02u   %ls\r\n", t.wHour, t.wMinute, t.wSecond, text);
	if(wcslen(app.activitytext) + wcslen(line) >= 8192) app.activitytext[0] = 0;
	wcscat(app.activitytext, line);
	SetWindowTextW(app.activity, app.activitytext);
	SendMessageW(app.activity, EM_SETSEL, (WPARAM)wcslen(app.activitytext), (LPARAM)wcslen(app.activitytext));
	SendMessageW(app.activity, EM_SCROLLCARET, 0, 0);
	SetWindowTextW(app.status, text);
}

static void
saveconfig(void)
{
	WritePrivateProfileStringW(L"engine", L"executable", app.enginepath, app.config);
	WritePrivateProfileStringW(L"engine", L"model", app.modelpath, app.config);
}

static void
readconfig(void)
{
	wchar_t base[MAX_PATH], fallback[MAX_PATH], legacy[MAX_PATH];
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
	if(n == 0 || n > MAX_PATH-60) return;
	if(!app_data_root(base,app.dataroot)) return;
	CreateDirectoryW(app.dataroot, NULL);
	swprintf(app.config, MAX_PATH, L"%ls\\settings.ini", app.dataroot);
	/* Keep the historical path for importing settings from the original app. */
	swprintf(legacy, MAX_PATH, L"%ls\\LTI\\communicator.ini", base);
	if(GetFileAttributesW(app.config)==INVALID_FILE_ATTRIBUTES) CopyFileW(legacy,app.config,TRUE);
	swprintf(fallback, MAX_PATH, L"%ls\\LocalAI\\llama.cpp\\b10566\\llama-server.exe", base);
	GetPrivateProfileStringW(L"engine", L"executable", fallback, app.enginepath, MAX_PATH, app.config);
	GetPrivateProfileStringW(L"engine", L"model", L"", app.modelpath, MAX_PATH, app.config);
	GetPrivateProfileStringW(L"library", L"folder", L"", app.libraryfolder, MAX_PATH, app.config);
	app.recursivevalue=GetPrivateProfileIntW(L"library",L"recursive",1,app.config)!=0;
    app.leftsize=(int)GetPrivateProfileIntW(L"layout",L"left",210,app.config);
    app.rightsize=(int)GetPrivateProfileIntW(L"layout",L"right",280,app.config);
    app.outputsize=(int)GetPrivateProfileIntW(L"layout",L"output",128,app.config);
    app.hideleft=GetPrivateProfileIntW(L"layout",L"hide_left",0,app.config)!=0;
    app.hideright=GetPrivateProfileIntW(L"layout",L"hide_right",0,app.config)!=0;
    app.hideoutput=GetPrivateProfileIntW(L"layout",L"hide_output",0,app.config)!=0;
    model_settings_load(app.config,app.modelpath,&app.settings);
}

static int
choosefile(int engine)
{
	OPENFILENAMEW of = {0};
	wchar_t path[MAX_PATH] = {0};
	wcscpy(path, engine ? app.enginepath : app.modelpath);
	of.lStructSize = sizeof(of); of.hwndOwner = app.window;
	of.lpstrFile = path; of.nMaxFile = MAX_PATH;
	of.lpstrTitle = engine ? L"Select local engine" : L"Select model";
	of.lpstrFilter = engine ? L"Local engine (*.exe)\0*.exe\0\0" : L"Model files (*.gguf)\0*.gguf\0\0";
	of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
	if(!GetOpenFileNameW(&of)) return 0;
	commitproperty();
	wcscpy(engine ? app.enginepath : app.modelpath, path);
	if(!engine) { SetWindowTextW(app.model, basenamew(path)); loadsettings(); }
	saveconfig(); InvalidateRect(app.window, NULL, FALSE);
	return 1;
}

static void
loadmodel(void)
{
	wchar_t error[256];
	unsigned short port = getport();
	if(app.busy || app.process.process) return;
	if(!port) { note(L"Enter a port from 1 to 65535."); return; }
	if(!setup_paths_ready(app.enginepath,app.modelpath) && !runsetup()) return;
	commitproperty();
	if(!engine_start_context(&app.process, app.enginepath, app.modelpath, port, app.settings.context_tokens, error, 256)) { note(error); return; }
	app.epoch++; app.health = EngineLoading; app.loadstarted = GetTickCount64();
	note(L"Loading model..."); refreshcontrols();
}

typedef struct Probe Probe;
struct Probe { HWND window; unsigned short port; int result; unsigned epoch; };

static DWORD WINAPI
probe(void *arg)
{
	Probe *p = arg;
	p->result = engine_probe(p->port);
	if(!PostMessageW(p->window, HealthReady, 0, (LPARAM)p)) free(p);
	return 0;
}

static void
pollengine(void)
{
	Probe *p;
	HANDLE thread;
	if(engine_exited(&app.process)) { app.health = EngineOffline; note(L"Engine exited. Check the selected engine and model."); refreshcontrols(); }
	if(app.checking || app.busy) return;
	app.probeport = getport();
	if(!app.probeport) { app.health = EngineOffline; refreshcontrols(); return; }
	p = calloc(1, sizeof(*p));
	if(!p) return;
	p->window = app.window; p->port = app.probeport; p->epoch = app.epoch;
	app.checking = 1;
	thread = CreateThread(NULL, 0, probe, p, 0, NULL);
	if(thread) CloseHandle(thread);
	else { free(p); app.checking = 0; }
}

static int
px(int n)
{
	return MulDiv(n, app.dpi, 96);
}

static wchar_t *
wide(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
	wchar_t *p, *lines;
	size_t i, j;
	if(n == 0) return NULL;
	p = malloc((size_t)n * sizeof(*p));
	if(p != NULL && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n)) {
		free(p);
		return NULL;
	}
	if(p == NULL) return NULL;
	/* Native edit controls expect CRLF. Keep model text unchanged in the core. */
	lines = malloc(((size_t)n * 2 + 1) * sizeof(*lines));
	if(lines == NULL) { free(p); return NULL; }
	for(i = 0, j = 0; p[i]; i++) {
		if(p[i] == L'\n' && (i == 0 || p[i-1] != L'\r')) lines[j++] = L'\r';
		lines[j++] = p[i];
	}
	lines[j] = 0;
	free(p);
	return lines;
}

static char *
utf8(const wchar_t *s)
{
	int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL);
	char *p;
	if(n == 0) return NULL;
	p = malloc((size_t)n);
	if(p != NULL && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, p, n, NULL, NULL)) {
		free(p);
		return NULL;
	}
	return p;
}

static void
appendstyle(const wchar_t *s, COLORREF color, int bold, int points)
{
	CHARFORMAT2W format = {0};
	SendMessage(app.transcript, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	format.cbSize = sizeof(format);
	format.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE | CFM_FACE;
	format.crTextColor = color; format.dwEffects = bold ? CFE_BOLD : 0;
	format.yHeight = points * 20; wcscpy(format.szFaceName, L"Tahoma");
	SendMessageW(app.transcript, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
	SendMessage(app.transcript, EM_REPLACESEL, FALSE, (LPARAM)s);
}

typedef struct ScanJob {
	HWND window;
	wchar_t folder[MAX_PATH];
	int recursive;
	Library library;
} ScanJob;

static DWORD WINAPI
scanworker(void *arg)
{
	ScanJob *job=arg;
	library_scan(&job->library,job->folder,job->recursive,&app.scancancel);
	if(InterlockedCompareExchange(&app.scancancel,0,0) ||
	    !PostMessageW(job->window,LibraryReady,0,(LPARAM)job)) free(job);
	return 0;
}

static void
scanlibrary(void)
{
	ScanJob *job;
	HANDLE thread;
	if(app.scanning) return;
	GetWindowTextW(app.folder,app.libraryfolder,MAX_PATH);
	app.recursivevalue=SendMessageW(app.recursive,BM_GETCHECK,0,0)==BST_CHECKED;
	if(!app.libraryfolder[0]) { note(L"Enter a local model folder in Library settings."); return; }
	WritePrivateProfileStringW(L"library",L"folder",app.libraryfolder,app.config);
	WritePrivateProfileStringW(L"library",L"recursive",app.recursivevalue?L"1":L"0",app.config);
	job=calloc(1,sizeof(*job)); if(!job) { note(L"Cannot scan: out of memory."); return; }
	job->window=app.window; job->recursive=app.recursivevalue; wcscpy(job->folder,app.libraryfolder);
	app.scanning=1; InterlockedExchange(&app.scancancel,0);
	thread=CreateThread(NULL,0,scanworker,job,0,NULL);
	if(!thread) { free(job); app.scanning=0; note(L"Cannot start model scan."); return; }
	CloseHandle(thread); EnableWindow(app.scan,FALSE); note(L"Reading local model headers...");
}

static void
modellist(ScanJob *job)
{
	unsigned i;
	LVITEMW item={0};
	wchar_t size[40],summary[160],name[288];
	Library *lib=malloc(sizeof(*lib));
	app.scanning=0; EnableWindow(app.scan,TRUE);
	if(!lib) { free(job); note(L"Cannot display library: out of memory."); return; }
	*lib=job->library; free(job);
	SendMessageW(app.models,WM_SETREDRAW,FALSE,0);
	ListView_DeleteAllItems(app.models); free(app.library); app.library=lib;
	modelproperties(NULL);
	for(i=0;i<lib->count;i++) {
		ModelInfo *m=&lib->models[i];
		swprintf(name,288,L"%ls%ls",m->projector?L"[Projector] ":L"",m->name);
		item.mask=LVIF_TEXT|LVIF_PARAM|LVIF_IMAGE; item.iItem=(int)i; item.iSubItem=0; item.pszText=name; item.lParam=(LPARAM)i;
		item.iImage=m->projector?ClassicEngine:ClassicModel;
		ListView_InsertItem(app.models,&item);
		swprintf(size,40,L"%.2f GB",(double)m->bytes/1000000000.0);
		ListView_SetItemText(app.models,(int)i,1,size);
	}
	SendMessageW(app.models,WM_SETREDRAW,TRUE,0); InvalidateRect(app.models,NULL,TRUE);
	swprintf(summary,160,L"%u local files. %u unreadable or skipped.%ls",lib->count,lib->skipped,lib->limited?L" Scan limit reached.":L"");
	note(summary); TabCtrl_SetCurSel(app.librarytabs,0); layout(); refreshtitle();
}

static int
runsetup(void)
{
	wchar_t *separator;
	if(app.busy || app.process.process) { note(L"Unload the current model before changing setup."); return 0; }
	commitproperty();
	if(!setup_dialog(app.window,app.normal,app.enginepath,app.modelpath)) return 0;
	loadsettings();
	saveconfig(); SetWindowTextW(app.model,basenamew(app.modelpath));
	if(!app.libraryfolder[0]) {
		wcscpy(app.libraryfolder,app.modelpath);
		separator=wcsrchr(app.libraryfolder,L'\\');
		if(separator) {
			/* Preserve a drive-root backslash, e.g. C:\\. */
			if(separator==app.libraryfolder+2 && app.libraryfolder[1]==L':') separator[1]=0;
			else *separator=0;
			SetWindowTextW(app.folder,app.libraryfolder); scanlibrary();
		} else app.libraryfolder[0]=0;
	}
	refreshcontrols(); note(L"Setup saved. Choose Load to start the selected model.");
	return 1;
}

static void
selectmodel(int use)
{
	int row=ListView_GetNextItem(app.models,-1,LVNI_SELECTED);
	ModelInfo *m;
	if(row<0 || !app.library || (unsigned)row>=app.library->count) { modelproperties(NULL); return; }
	m=&app.library->models[row];
	modelproperties(m);
	if(!use) return;
	if(m->projector) { note(L"This is a projector companion. Select a language model."); return; }
	if(app.busy || app.process.process || app.health==EngineReady) { note(L"Unload the current model before selecting another."); return; }
	commitproperty();
	wcscpy(app.modelpath,m->path); SetWindowTextW(app.model,m->name); loadsettings(); saveconfig(); note(L"Model selected. Choose Load to start it.");
}

static void
render(void)
{
	Conversation *c = &app.chat.conversation;
	wchar_t *text;
	size_t i;
	SetWindowText(app.transcript, L"");
	if(app.budget.omitted_messages) {
		wchar_t summary[192];
		swprintf(summary,192,L"Last request: %zu older exchanges were outside the model's context. All remain saved below.\r\n\r\n",app.budget.omitted_messages/2);
		appendstyle(summary,Muted,0,9);
	}
	if(c->count == 0) {
		appendstyle(L"New conversation\r\n\r\n", BlueInk, 1, 10);
		appendstyle(app.health==EngineReady?L"Ready. Enter a message below.\r\n\r\n":L"Select a local model, then choose Load.\r\n\r\n", Muted, 0, 9);
		appendstyle(L"Chats and drafts are saved locally.\r\n", Muted, 0, 9);
	}
	for(i = 0; i < c->count; i++) {
		appendstyle(i % 2 == 0 ? L"You\r\n" : L"Assistant\r\n", i % 2 == 0 ? Muted : BlueInk, 1, 9);
		text = wide(c->messages[i].text);
		if(text != NULL) { appendstyle(text, Ink, 0, 10); free(text); }
		appendstyle(L"\r\n\r\n", Ink, 0, 10);
	}
	SendMessage(app.transcript, EM_SCROLLCARET, 0, 0);
	InvalidateRect(app.window, NULL, FALSE);
	InvalidateRect(app.modules, NULL, FALSE);
}

static void
refreshtitle(void)
{
	wchar_t name[256],title[320],previous[320],*extension;
	unsigned i;
	wcsncpy(name,basenamew(app.modelpath),255); name[255]=0;
	extension=wcsrchr(name,L'.');
	if(extension && !_wcsicmp(extension,L".gguf")) *extension=0;
	for(i=0;name[i];i++) if(name[i]==L'_') name[i]=L' ';
	if(app.library) for(i=0;i<app.library->count;i++) {
		if(!_wcsicmp(app.library->models[i].path,app.modelpath)) {
			wcscpy(name,app.library->models[i].name); break;
		}
	}
	if(!app.process.process) wcscpy(title,app.busy?L"lcb-ai: Working":
	    app.health==EngineReady?L"lcb-ai: Connected":L"lcb-ai: Idle");
	else swprintf(title,320,L"lcb-ai: %ls %ls",app.busy?L"Working":
	    app.health==EngineReady?L"Loaded":L"Loading",name);
	GetWindowTextW(app.window,previous,320);
	if(wcscmp(previous,title)) SetWindowTextW(app.window,title);
}

static void
refreshcontrols(void)
{
	refreshtitle();
	EnableWindow(app.send, !app.busy && app.health == EngineReady);
    EnableWindow(app.stop,app.busy && app.cancel && WaitForSingleObject(app.cancel,0)!=WAIT_OBJECT_0);
    EnableWindow(app.chattabs,!app.busy); EnableWindow(app.closetab,!app.busy);
    EnableWindow(app.generation,!app.busy);
	EnableWindow(app.modules, !app.busy);
	EnableWindow(app.workspace, !app.busy);
	EnableWindow(app.newworkspace, !app.busy);
	EnableWindow(app.editworkspace, !app.busy);
	EnableWindow(app.fresh, !app.busy);
	EnableWindow(app.port, !app.busy && !app.process.process);
	EnableWindow(app.load, !app.busy && !app.process.process && app.health != EngineReady);
	EnableWindow(app.unload, !app.busy && app.process.process != NULL);
	EnableWindow(app.browse, !app.busy && !app.process.process);
	EnableWindow(app.engine, !app.busy && !app.process.process);
	SendMessage(app.prompt, EM_SETREADONLY, app.busy, 0);
	InvalidateRect(app.window, NULL, FALSE);
}

static void
busy(int value)
{
	app.busy = value;
	refreshcontrols();
}

static void
workfree(Work *w)
{
	if(w->cancel) CloseHandle(w->cancel);
	free(w->request);
	free(w->prompt);
	free(w->answer);
	free(w->instruction);
	free(w);
}

static void streamupdate(const char *text,void *context)
{
    Work *w=context;
    char *copy=malloc(strlen(text)+1);
    if(!copy) return;
    strcpy(copy,text);
    if(!PostMessageW(w->target,StreamReady,0,(LPARAM)copy)) free(copy);
}
static DWORD WINAPI
complete(void *arg)
{
    Work *w=arg;
    Module module={"Workspace",w->instruction};
    if(local_prepare(w->port,w->modelpath,w->conversation,&module,w->prompt,&w->settings,
        w->cancel,&w->request,&w->budget,w->error,sizeof(w->error))) {
        PostMessageW(w->target,BudgetReady,0,(LPARAM)w);
        w->ok=local_stream_report(w->port,w->request,w->cancel,streamupdate,w,&w->answer,&w->report,w->error,sizeof(w->error));
    }
    if(WaitForSingleObject(w->cancel,0)==WAIT_OBJECT_0) w->ok=2;
    if(!PostMessageW(w->target,ReplyReady,0,(LPARAM)w)) workfree(w);
    return 0;
}
static void stopreply(void)
{
    if(app.busy && app.cancel) {
        SetEvent(app.cancel); EnableWindow(app.stop,FALSE); note(L"Stopping generation...");
    }
}

static void
submit(void)
{
	wchar_t porttext[16], *end, *input;
	unsigned long port;
	int n;
	HANDLE thread;
	Work *w;
	Module module;
	if(app.busy || app.health != EngineReady) return;
	commitproperty();
	if(!savechat()) return;
	module.name=app.store.workspaces[app.selected].name;
	module.instruction=app.store.workspaces[app.selected].prompt;
	GetWindowText(app.port, porttext, 16);
	port = wcstoul(porttext, &end, 10);
	if(*end != 0 || port == 0 || port > 65535) {
		SetWindowText(app.status, L"Enter a port from 1 to 65535.");
		return;
	}
	n = GetWindowTextLength(app.prompt);
	if(n == 0) return;
	input = calloc((size_t)n + 1, sizeof(*input));
	w = calloc(1, sizeof(*w));
	if(input == NULL || w == NULL) { free(input); free(w); return; }
	GetWindowText(app.prompt, input, n + 1);
	w->prompt = utf8(input);
	free(input);
	w->instruction=malloc(strlen(module.instruction)+1);
	if(w->instruction) strcpy(w->instruction,module.instruction);
	if(!w->prompt || !w->instruction || strlen(w->prompt)>LcbMaxPrompt) {
		SetWindowText(app.status, L"Cannot prepare message. Maximum prompt size is 16 KiB; saved history has no exchange limit.");
		workfree(w);
		return;
	}
	w->port = (unsigned short)port;
	w->target = app.window;
	w->conversation=&app.chat.conversation; w->settings=app.settings;
	wcscpy(w->modelpath,app.modelpath);
	w->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!w->cancel) { workfree(w); note(L"Cannot create request."); return; }
    app.cancel=w->cancel; app.work=w; app.firstseconds=0;
    app.started = GetTickCount64();
	busy(1);
	note(L"Checking the model and counting conversation tokens...");
	render();
	if(app.chat.conversation.count == 0) SetWindowTextW(app.transcript,L"");
	appendstyle(L"You\r\n",Muted,1,9);
	input=wide(w->prompt);
	if(input) { appendstyle(input,Ink,0,10); free(input); }
	appendstyle(L"\r\n\r\nAssistant\r\n",BlueInk,1,9);
    { CHARRANGE caret; SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&caret); app.answerstart=caret.cpMax; }
    app.streamchars=0;
    appendstyle(L"Waiting for reply...",Muted,0,9);
	SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
	thread = CreateThread(NULL, 0, complete, w, 0, NULL);
	if(thread == NULL) {
		app.cancel=NULL; app.work=NULL; busy(0);
		render();
		SetWindowText(app.status, L"Cannot start the local request worker.");
		workfree(w);
	} else CloseHandle(thread);
}

static void
received(Work *w)
{
	Conversation *c = &app.chat.conversation;
	wchar_t *answer = w->ok && w->answer ? wide(w->answer) : NULL;
	wchar_t *error;
	size_t before = c->count, bytes = c->bytes;
	app.cancel=NULL; app.work=NULL; busy(0);
	if(answer != NULL && conversation_add(c, "user", w->prompt) &&
	    conversation_add(c, "assistant", w->answer)) {
		wchar_t summary[128];
		app.lastseconds = (double)(GetTickCount64()-app.started)/1000.0;
		swprintf(summary, 128, L"%ls in %.2f s. %d tokens. First text: %.2f s.", w->ok==2?L"Stopped; partial reply saved":w->report.finish==FinishLength?L"Response limit reached":L"Reply received", app.lastseconds, w->report.tokens, app.firstseconds);
		note(summary);
		SetWindowText(app.prompt, L"");
		if(before==0) {
			size_t n=strlen(w->prompt), j;
			if(n>=StoreName) n=StoreName-1;
			while(n && ((unsigned char)w->prompt[n]&0xc0)==0x80) n--;
			memcpy(app.chat.info.title,w->prompt,n); app.chat.info.title[n]=0;
			for(j=0;j<n;j++) if(app.chat.info.title[j]=='\r' || app.chat.info.title[j]=='\n') app.chat.info.title[j]=' ';
		}
		app.dirty=1; savechat(); listchats(); synctabs();
		render();
	} else {
		while(c->count > before) free(c->messages[--c->count].text);
		c->bytes = bytes;
		render();
		error = wide(w->ok==2 ? "Generation stopped. Your draft was retained." : w->error[0] ? w->error : "Cannot store answer: invalid UTF-8 or out of memory.");
		note(error ? error : L"Request failed.");
		free(error);
	}
	free(answer);
	workfree(w);
	SetFocus(app.prompt);
}

static HWND
control(const wchar_t *kind, const wchar_t *text, DWORD style, int id)
{
	DWORD ex=(!wcscmp(kind,L"EDIT") || !wcscmp(kind,L"LISTBOX") || !wcscmp(kind,WC_LISTVIEWW) || !wcscmp(kind,WC_TREEVIEWW))?WS_EX_CLIENTEDGE:0;
	HWND w = CreateWindowEx(ex, kind, text, WS_CHILD | WS_VISIBLE | (style&~WS_BORDER),
	    0, 0, 1, 1, app.window, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
	SendMessage(w, WM_SETFONT, (WPARAM)app.normal, TRUE);
	SetWindowTheme(w, L"", L"");
	SetWindowSubclass(w,classiccontrol,1,0);
	return w;
}

static void tooltips(void)
{
    HWND tips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,app.window,NULL,GetModuleHandleW(NULL),NULL);
    HWND buttons[]={app.fresh,app.load,app.unload,app.engine,app.browse,app.stop,app.closetab,app.newworkspace,app.editworkspace};
    const wchar_t *texts[]={L"New conversation (Ctrl+N)",L"Load the selected local model",L"Unload the model from memory",
        L"Choose the local llama.cpp engine",L"Choose an existing GGUF file",L"Stop generation (Escape)",
        L"Close this tab; saved chat is retained (Ctrl+W)",L"Create a workspace",L"Edit workspace name and system instructions"};
    size_t i;
    for(i=0;tips && i<sizeof(buttons)/sizeof(buttons[0]);i++) {
        TOOLINFOW tool={0}; tool.cbSize=sizeof(tool); tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tool.hwnd=app.window; tool.uId=(UINT_PTR)buttons[i]; tool.lpszText=(wchar_t *)texts[i];
        SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tool);
    }
}

static void
place(HWND w, int x, int y, int width, int height)
{
	MoveWindow(w, px(x), px(y), px(width), px(height), TRUE);
}

static RECT
rect(int x, int y, int w, int h)
{
	RECT r = {px(x), px(y), px(x+w), px(y+h)};
	return r;
}

static void
fill(HDC dc, RECT r, COLORREF color)
{
	COLORREF old=SetDCBrushColor(dc,color);
	FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH)); SetDCBrushColor(dc,old);
}

static void
line(HDC dc, int x, int y, int x2, int y2, COLORREF color)
{
	HPEN old = SelectObject(dc,GetStockObject(DC_PEN));
	COLORREF before=SetDCPenColor(dc,color);
	MoveToEx(dc, px(x), px(y), NULL); LineTo(dc, px(x2), px(y2));
	SelectObject(dc,old); SetDCPenColor(dc,before);
}

static void
label(HDC dc, RECT r, const wchar_t *s, COLORREF color, HFONT font, UINT flags)
{
	HFONT old = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
	DrawTextW(dc, s, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | flags);
	SelectObject(dc, old);
}

static void
panel(HDC dc, int x, int y, int w, int h, const wchar_t *name, int selected, int icon)
{
	RECT r=rect(x,y,w,h),title=rect(x+2,y+2,w-4,18),well=rect(x+2,y+21,w-4,h-23);
	fill(dc,r,Face); classic_edge(dc,r,1);
	fill(dc,title,selected?RGB(76,112,157):RGB(218,216,201));
	line(dc,x+2,y+2,x+w-2,y+2,selected?RGB(145,175,205):Light);
	line(dc,x+2,y+20,x+w-2,y+20,Shadow);
	if(app.icons) ImageList_Draw(app.icons,icon,dc,px(x+5),px(y+3),ILD_TRANSPARENT);
	label(dc,rect(x+25,y+2,w-30,18),name,selected?Paper:Ink,app.heading,DT_LEFT);
	fill(dc,well,Paper); classic_edge(dc,well,0);
}

static void
paint(HDC dc)
{
    int w=app.width,h=app.height;
    PaneLayout p=workbench_layout(w,h,app.hideleft?0:app.leftsize,app.hideright?0:app.rightsize,app.hideoutput?0:app.outputsize);
    int x=p.left+5,cw=p.center,tab=TabCtrl_GetCurSel(app.librarytabs);
    wchar_t text[256];
    fill(dc,rect(0,0,w,h),Face);
    classic_edge(dc,rect(2,1,w-4,31),1);
    line(dc,88,5,88,26,Shadow); line(dc,271,5,271,26,Shadow);
    label(dc,rect(w-96,3,82,23),L"lcb-ai",BlueInk,app.heading,DT_RIGHT);
    if(!app.hideleft) {
        panel(dc,4,36,p.left-4,p.bottom-41,L"Workspace Explorer",app.activepane==0,ClassicFolder);
        label(dc,rect(12,p.bottom-27,p.left-20,18),app.dirty?L"Unsaved changes":L"Saved locally",Muted,app.normal,DT_LEFT);
    }
    panel(dc,x,36,cw,p.bottom-41,L"Conversations",app.activepane==1,ClassicDocument);
    fill(dc,rect(x+2,p.composer,cw-4,21),Face);
    line(dc,x+2,p.composer,x+cw-2,p.composer,Shadow);
    label(dc,rect(x+8,p.composer+1,100,19),L"Message",Ink,app.normal,DT_LEFT);
    label(dc,rect(x+cw-181,p.composer+1,174,19),L"Ctrl+Enter to send",Muted,app.normal,DT_RIGHT);
    if(!app.hideright) {
        panel(dc,p.rightx,36,p.right,p.bottom-41,L"Properties and Models",app.activepane==2,ClassicSettings);
        if(tab==0) {
            fill(dc,rect(p.rightx+3,p.bottom-207,p.right-6,21),Face);
            label(dc,rect(p.rightx+9,p.bottom-207,p.right-18,21),L"Model properties",Ink,app.heading,DT_LEFT);
        } else fill(dc,rect(p.rightx+3,84,p.right-6,p.bottom-94),Face);
    }
    if(!app.hideoutput) panel(dc,4,p.bottom,w-8,h-p.bottom-29,L"Output / Session activity",app.activepane==3,ClassicOutput);
    classic_edge(dc,rect(4,h-22,206,20),0);
    classic_edge(dc,rect(213,h-22,260,20),0);
    classic_edge(dc,rect(476,h-22,w-480,20),0);
    label(dc,rect(12,h-22,190,20),app.busy?L"Generating...":app.health==EngineReady?L"Engine ready":L"Engine offline",Ink,app.normal,DT_LEFT);
    if(app.busy) swprintf(text,256,L"Elapsed: %.1f s",(double)(GetTickCount64()-app.started)/1000.0);
    else swprintf(text,256,L"Last reply: %.2f s",app.lastseconds);
    label(dc,rect(221,h-22,244,20),text,Muted,app.normal,DT_LEFT);
    if(app.budget.context_tokens)
        swprintf(text,256,L"%zu saved exchanges | Context: %d + %d reply / %d | %zu older excluded",
            app.chat.conversation.count/2,app.budget.prompt_tokens,app.budget.response_tokens,
            app.budget.context_tokens,app.budget.omitted_messages/2);
    else swprintf(text,256,L"127.0.0.1:%u   |   %zu saved exchanges   |   Context counted before sending",getport(),app.chat.conversation.count/2);
    label(dc,rect(485,h-22,w-498,20),text,Muted,app.normal,DT_LEFT);
}

static void
drawbutton(DRAWITEMSTRUCT *d)
{
	wchar_t text[96];
	RECT r = d->rcItem, t = r;
	int icon=-1,hot=app.hotbutton==d->hwndItem;
	int pressed = (d->itemState & ODS_SELECTED) != 0;
	int disabled = (d->itemState & ODS_DISABLED) != 0;
	fill(d->hDC,r,disabled?Face:pressed?Blue:hot?Light:d->CtlID==IdSend?Blue:Face);
	classic_edge(d->hDC,r,!pressed);
	GetWindowTextW(d->hwndItem,text,96);
	switch(d->CtlID) {
	case IdNew: icon=ClassicDocument; break;
	case IdLoad: case IdSend: icon=ClassicRun; break;
	case IdStop: case IdUnload: icon=ClassicStop; break;
	case IdEngine: icon=ClassicEngine; break;
	case IdBrowse: icon=ClassicFolder; break;
	}
	if(icon>=0 && app.icons) {
		ImageList_DrawEx(app.icons,icon,d->hDC,r.left+px(5)+(pressed?1:0),
		    r.top+(r.bottom-r.top-px(16))/2+(pressed?1:0),0,0,CLR_NONE,Face,
		    disabled?ILD_BLEND50:ILD_TRANSPARENT);
		t.left+=px(23); t.right-=px(4);
	}
	if(pressed) OffsetRect(&t,1,1);
	{
		HFONT old=SelectObject(d->hDC,app.normal);
		SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,disabled?Muted:Ink);
		DrawTextW(d->hDC,text,-1,&t,DT_SINGLELINE|DT_VCENTER|DT_CENTER|
		    ((d->itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0));
		SelectObject(d->hDC,old);
	}
	if(d->itemState & ODS_FOCUS) { InflateRect(&t,-4,-4); DrawFocusRect(d->hDC,&t); }
}

static void
drawtab(DRAWITEMSTRUCT *d)
{
	wchar_t text[80];
	TCITEMW item={0};
	RECT r=d->rcItem,t=r;
	int selected=(d->itemState&ODS_SELECTED)!=0;
	item.mask=TCIF_TEXT; item.pszText=text; item.cchTextMax=80;
	if(!TabCtrl_GetItem(d->hwndItem,(int)d->itemID,&item)) return;
	fill(d->hDC,r,selected?Light:Face);
	if(selected) {
		RECT stripe=r; stripe.bottom=stripe.top+px(2); fill(d->hDC,stripe,RGB(76,112,157));
	}
	InflateRect(&t,-px(5),0);
	label(d->hDC,t,text,selected?BlueInk:Ink,selected?app.heading:app.normal,DT_CENTER);
	if(d->itemState&ODS_FOCUS) { InflateRect(&t,-2,-2); DrawFocusRect(d->hDC,&t); }
}

static LRESULT CALLBACK
classiccontrol(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
	(void)data;
	if(message==WM_MOUSEMOVE && (GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW &&
	    (window==app.fresh || window==app.load || window==app.unload || window==app.engine ||
	    window==app.browse || window==app.send || window==app.scan ||
	    window==app.newworkspace || window==app.editworkspace || window==app.stop || window==app.closetab)) {
		if(app.hotbutton!=window) {
			TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0};
			HWND previous=app.hotbutton;
			app.hotbutton=window; TrackMouseEvent(&track);
			if(previous) InvalidateRect(previous,NULL,FALSE);
			InvalidateRect(window,NULL,FALSE);
		}
	} else if(message==WM_MOUSELEAVE && app.hotbutton==window) {
		app.hotbutton=NULL; InvalidateRect(window,NULL,FALSE);
	} else if(message==WM_SETFOCUS) {
		int pane=app.activepane;
		if(window==app.workspace || window==app.modules || window==app.newworkspace || window==app.editworkspace) pane=0;
		else if(window==app.transcript || window==app.prompt || window==app.master || window==app.chattabs || window==app.send) pane=1;
		else if(window==app.models || window==app.details || window==app.librarytabs || window==app.folder || window==app.recursive || window==app.scan || window==app.generation || window==app.value) pane=2;
		else if(window==app.activity) pane=3;
		if(pane!=app.activepane) { app.activepane=pane; InvalidateRect(app.window,NULL,FALSE); }
	} else if(message==WM_NCDESTROY) {
		if(app.hotbutton==window) app.hotbutton=NULL;
		RemoveWindowSubclass(window,classiccontrol,id);
	}
	return DefSubclassProc(window,message,wp,lp);
}

static void
layout(void)
{
    RECT r,margin;
    PaneLayout p;
    int w,h,x,cw,tab,left,right,out;
    GetClientRect(app.window,&r);
    w=app.width=MulDiv(r.right,96,app.dpi); h=app.height=MulDiv(r.bottom,96,app.dpi);
    p=workbench_layout(w,h,app.hideleft?0:app.leftsize,app.hideright?0:app.rightsize,app.hideoutput?0:app.outputsize);
    x=p.left+5; cw=p.center; tab=TabCtrl_GetCurSel(app.librarytabs);
    left=!app.hideleft; right=!app.hideright; out=!app.hideoutput;
    place(app.fresh,6,4,76,23); place(app.load,95,4,79,23); place(app.unload,178,4,85,23);
    place(app.engine,279,4,78,23); place(app.browse,362,4,84,23);
    place(app.model,453,5,w-645,21); place(app.port,w-185,5,62,21);
    ShowWindow(app.workspace,SW_HIDE); ShowWindow(app.master,SW_HIDE);
    place(app.newworkspace,10,61,87,23); place(app.editworkspace,101,61,p.left-109,23);
    place(app.modules,9,89,p.left-14,p.bottom-122);
    ShowWindow(app.modules,left?SW_SHOW:SW_HIDE); ShowWindow(app.newworkspace,left?SW_SHOW:SW_HIDE);
    ShowWindow(app.editworkspace,left?SW_SHOW:SW_HIDE);
    place(app.chattabs,x+2,57,cw-29,25); place(app.closetab,x+cw-25,58,22,22);
    place(app.transcript,x+3,83,cw-6,p.composer-85);
    GetClientRect(app.transcript,&margin); InflateRect(&margin,-px(10),-px(10));
    SendMessageW(app.transcript,EM_SETRECT,0,(LPARAM)&margin);
    place(app.prompt,x+7,p.composer+25,cw-88,69);
    place(app.send,x+cw-74,p.composer+25,66,24);
    place(app.stop,x+cw-74,p.composer+55,66,24);
    place(app.librarytabs,p.rightx+2,57,p.right-4,25);
    place(app.models,p.rightx+5,84,p.right-10,p.bottom-294);
    ListView_SetColumnWidth(app.models,0,px(p.right-82)); ListView_SetColumnWidth(app.models,1,px(68));
    place(app.details,p.rightx+5,p.bottom-183,p.right-10,172);
    ListView_SetColumnWidth(app.details,0,px(88)); ListView_SetColumnWidth(app.details,1,px(p.right-119));
    place(app.folderlabel,p.rightx+9,97,p.right-18,20);
    place(app.folder,p.rightx+9,120,p.right-18,23);
    place(app.recursive,p.rightx+9,153,p.right-18,22); place(app.scan,p.rightx+9,188,119,25);
    place(app.generation,p.rightx+6,91,p.right-12,190);
    ListView_SetColumnWidth(app.generation,0,px(124)); ListView_SetColumnWidth(app.generation,1,px(p.right-141));
    place(app.genhelp,p.rightx+10,292,p.right-20,p.bottom-302);
    ShowWindow(app.librarytabs,right?SW_SHOW:SW_HIDE);
    ShowWindow(app.models,right && tab==0?SW_SHOW:SW_HIDE); ShowWindow(app.details,right && tab==0?SW_SHOW:SW_HIDE);
    ShowWindow(app.generation,right && tab==1?SW_SHOW:SW_HIDE); ShowWindow(app.genhelp,right && tab==1?SW_SHOW:SW_HIDE);
    ShowWindow(app.folderlabel,right && tab==2?SW_SHOW:SW_HIDE); ShowWindow(app.folder,right && tab==2?SW_SHOW:SW_HIDE);
    ShowWindow(app.recursive,right && tab==2?SW_SHOW:SW_HIDE); ShowWindow(app.scan,right && tab==2?SW_SHOW:SW_HIDE);
    place(app.activity,9,p.bottom+24,w-18,h-p.bottom-59);
    ShowWindow(app.activity,out?SW_SHOW:SW_HIDE); ShowWindow(app.status,SW_HIDE);
    ShowWindow(app.value,SW_HIDE); app.property=-1;
    InvalidateRect(app.window,NULL,FALSE);
}

static int
savechat(void)
{
	wchar_t *input;
	char *text;
	int n;
	if(!app.dirty) return 1;
	n=GetWindowTextLengthW(app.prompt);
	input=calloc((size_t)n+1,sizeof(*input));
	if(!input) { note(L"Cannot save draft: out of memory."); return 0; }
	GetWindowTextW(app.prompt,input,n+1); text=utf8(input); free(input);
	if(!text || strlen(text)>LcbMaxPrompt) { free(text); note(L"Cannot save draft: invalid or oversized text."); return 0; }
	strcpy(app.chat.draft,text); free(text);
	if(!store_save(&app.store,&app.chat)) { note(app.store.error); return 0; }
	app.dirty=0; KillTimer(app.window,3); InvalidateRect(app.window,NULL,FALSE); return 1;
}

static void
listworkspaces(void)
{
	size_t i;
	wchar_t *name;
	SendMessageW(app.workspace,CB_RESETCONTENT,0,0);
	for(i=0;i<app.store.nworkspaces;i++) {
		name=wide(app.store.workspaces[i].name);
		SendMessageW(app.workspace,CB_ADDSTRING,0,(LPARAM)(name?name:L"Workspace")); free(name);
	}
	SendMessageW(app.workspace,CB_SETCURSEL,app.selected,0);
}

static void
listchats(void)
{
    size_t i,j;
    TVINSERTSTRUCTW item={0};
    HTREEITEM parent,selected=NULL;
    wchar_t *name;
    app.treebusy=1;
    SendMessageW(app.modules,WM_SETREDRAW,FALSE,0); TreeView_DeleteAllItems(app.modules);
    item.hInsertAfter=TVI_LAST;
    item.item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
    for(i=0;i<app.store.nworkspaces;i++) {
        name=wide(app.store.workspaces[i].name); item.hParent=TVI_ROOT;
        item.item.pszText=name?name:L"Workspace"; item.item.lParam=-(LPARAM)(i+1);
        item.item.iImage=item.item.iSelectedImage=ClassicFolder;
        parent=TreeView_InsertItem(app.modules,&item); free(name);
        for(j=0;j<app.store.nchats;j++) if(!strcmp(app.store.chats[j].workspace,app.store.workspaces[i].id)) {
            HTREEITEM child;
            name=wide(app.store.chats[j].title); item.hParent=parent;
            item.item.pszText=name?name:L"Chat"; item.item.lParam=(LPARAM)(j+1);
            item.item.iImage=item.item.iSelectedImage=ClassicDocument;
            child=TreeView_InsertItem(app.modules,&item); free(name);
            if(!strcmp(app.store.chats[j].id,app.chat.info.id)) selected=child;
        }
        TreeView_Expand(app.modules,parent,TVE_EXPAND);
    }
    if(selected) TreeView_SelectItem(app.modules,selected);
    SendMessageW(app.modules,WM_SETREDRAW,TRUE,0); InvalidateRect(app.modules,NULL,TRUE);
    app.treebusy=0;
}

static void
showchat(void)
{
    memset(&app.budget,0,sizeof(app.budget));
	wchar_t *draft=wide(app.chat.draft), id[33];
	wchar_t *master=wide(app.store.workspaces[app.selected].prompt);
	SetWindowTextW(app.master,master?master:L""); free(master);
	app.restoring=1; SetWindowTextW(app.prompt,draft?draft:L""); app.restoring=0; free(draft);
	app.dirty=0; KillTimer(app.window,3);
	swprintf(id,33,L"%hs",app.chat.info.id);
	WritePrivateProfileStringW(L"session",L"chat",id,app.config);
	opentab(); listchats(); render(); layout();
}

static void activatechat(const char *id)
{
    size_t i;
    if(app.busy || !strcmp(id,app.chat.info.id)) { synctabs(); return; }
    if(!savechat() || !store_load(&app.store,id,&app.chat)) { note(app.store.error); synctabs(); listchats(); return; }
    for(i=0;i<app.store.nworkspaces;i++) if(!strcmp(app.chat.info.workspace,app.store.workspaces[i].id)) break;
    app.selected=i; listworkspaces(); showchat();
}

static void
switchworkspace(void)
{
	LRESULT index=SendMessageW(app.workspace,CB_GETCURSEL,0,0);
	size_t i;
	int ok;
	if(app.busy || index<0 || (size_t)index>=app.store.nworkspaces) return;
	if(!savechat()) { listworkspaces(); return; }
	for(i=0;i<app.store.nchats;i++) if(strcmp(app.store.chats[i].workspace,app.store.workspaces[index].id)==0) break;
	ok=i<app.store.nchats ? store_load(&app.store,app.store.chats[i].id,&app.chat) :
	    store_new(&app.store,app.store.workspaces[index].id,&app.chat);
	if(!ok) { note(app.store.error); listworkspaces(); return; }
	app.selected=(size_t)index; showchat();
}

static void
workspaceedit(int fresh)
{
	Workspace *next;
	wchar_t name[StoreName*2], prompt[LcbMaxPrompt*2+1], *text;
	char *narrowname, *narrowprompt;
	int ok;
	if(app.busy || !savechat()) return;
	next=calloc(1,sizeof(*next)); if(!next) return;
	if(!fresh) *next=app.store.workspaces[app.selected];
	text=wide(next->name); wcscpy(name,text?text:L""); free(text);
	text=wide(next->prompt); wcscpy(prompt,text?text:L""); free(text);
	while(edit_workspace(app.window,app.normal,fresh?L"New workspace":L"Workspace settings",name,StoreName*2,prompt,LcbMaxPrompt*2+1)) {
		narrowname=utf8(name); narrowprompt=utf8(prompt);
		ok=narrowname && narrowprompt && strlen(narrowname)<StoreName && strlen(narrowprompt)<=LcbMaxPrompt;
		if(ok) { strcpy(next->name,narrowname); strcpy(next->prompt,narrowprompt); }
		free(narrowname); free(narrowprompt);
		if(ok && store_workspace(&app.store,next)) {
			if(fresh) {
				if(store_new(&app.store,next->id,&app.chat)) app.selected=app.store.nworkspaces-1;
				else { note(app.store.error); listworkspaces(); break; }
			}
			listworkspaces(); showchat(); note(L"Workspace saved."); break;
		}
		MessageBoxW(app.window,ok?app.store.error:L"Workspace text is too long or invalid.",L"lcb-ai",MB_OK|MB_ICONERROR);
	}
	free(next);
}

static int
openstore(void)
{
	wchar_t last[33];
	char id[33]={0};
	size_t i;
	Workspace *w;
	if(!app.dataroot[0] || !store_open(&app.store,app.dataroot)) return 0;
	if(!app.store.nworkspaces) {
		w=calloc(1,sizeof(*w)); if(!w) return 0;
		strcpy(w->name,"General"); strcpy(w->prompt,lcb_modules[0].instruction);
		if(!store_workspace(&app.store,w)) { free(w); return 0; } free(w);
	}
	GetPrivateProfileStringW(L"session",L"chat",L"",last,33,app.config);
	WideCharToMultiByte(CP_UTF8,0,last,-1,id,33,NULL,NULL);
	for(i=0;i<app.store.nchats;i++) if(strcmp(app.store.chats[i].id,id)==0) break;
	if(i==app.store.nchats) {
		for(i=0;i<app.store.nchats;i++) if(strcmp(app.store.chats[i].workspace,app.store.workspaces[0].id)==0) break;
	}
	if(i<app.store.nchats) {
		if(!store_load(&app.store,app.store.chats[i].id,&app.chat)) return 0;
	} else if(!store_new(&app.store,app.store.workspaces[0].id,&app.chat)) return 0;
	for(i=0;i<app.store.nworkspaces;i++) if(strcmp(app.store.workspaces[i].id,app.chat.info.workspace)==0) break;
	app.selected=i; return 1;
}

static void saveint(const wchar_t *section,const wchar_t *key,int value)
{
    wchar_t text[32]; swprintf(text,32,L"%d",value);
    WritePrivateProfileStringW(section,key,text,app.config);
}
static void savelayout(void)
{
    int i; wchar_t key[32],id[33];
    saveint(L"session",L"tabs",app.ntabs);
    for(i=0;i<app.ntabs;i++) {
        swprintf(key,32,L"tab%d",i); swprintf(id,33,L"%hs",app.tabs[i]);
        WritePrivateProfileStringW(L"session",key,id,app.config);
    }
    saveint(L"layout",L"left",app.leftsize); saveint(L"layout",L"right",app.rightsize);
    saveint(L"layout",L"output",app.outputsize);
    saveint(L"layout",L"hide_left",app.hideleft); saveint(L"layout",L"hide_right",app.hideright);
    saveint(L"layout",L"hide_output",app.hideoutput);
}
static void synctabs(void)
{
    int i,selected=0;
    size_t j;
    TCITEMW item={0};
    wchar_t *name,labeltext[40];
    TabCtrl_DeleteAllItems(app.chattabs);
    item.mask=TCIF_TEXT;
    for(i=0;i<app.ntabs;i++) {
        for(j=0;j<app.store.nchats;j++) if(!strcmp(app.tabs[i],app.store.chats[j].id)) break;
        name=wide(j<app.store.nchats?app.store.chats[j].title:"Chat");
        wcsncpy(labeltext,name?name:L"Chat",28); labeltext[28]=0;
        if(name && wcslen(name)>28) wcscat(labeltext,L"...");
        free(name); item.pszText=labeltext; TabCtrl_InsertItem(app.chattabs,i,&item);
        if(!strcmp(app.tabs[i],app.chat.info.id)) selected=i;
    }
    TabCtrl_SetCurSel(app.chattabs,selected);
}
static void opentab(void)
{
    int i;
    for(i=0;i<app.ntabs;i++) if(!strcmp(app.tabs[i],app.chat.info.id)) break;
    if(i==app.ntabs) {
        if(app.ntabs==32) { memmove(app.tabs,app.tabs+1,31*sizeof(app.tabs[0])); app.ntabs--; }
        strcpy(app.tabs[app.ntabs++],app.chat.info.id);
    }
    synctabs();
}
static void closetab(void)
{
    int index=TabCtrl_GetCurSel(app.chattabs);
    if(app.busy || index<0 || !savechat()) return;
    if(app.ntabs==1) {
        if(!store_new(&app.store,app.store.workspaces[app.selected].id,&app.chat)) { note(app.store.error); return; }
        app.ntabs=0; showchat(); return;
    }
    memmove(app.tabs+index,app.tabs+index+1,(size_t)(app.ntabs-index-1)*sizeof(app.tabs[0]));
    app.ntabs--;
    activatechat(app.tabs[index<app.ntabs?index:app.ntabs-1]);
}
static void loadsettings(void)
{
    model_settings_load(app.config,app.modelpath,&app.settings);
    memset(&app.budget,0,sizeof(app.budget));
    if(app.generation) generationrows();
}
static void generationrows(void)
{
    const wchar_t *names[]={L"Response tokens",L"Temperature",L"Top P",L"Context (reload)",
        L"Thinking",L"Repeat penalty",L"DRY strength"};
    wchar_t value[64];
    LVITEMW item={0};
    int i;
    ListView_DeleteAllItems(app.generation);
    for(i=0;i<7;i++) {
        item.mask=LVIF_TEXT; item.iItem=i; item.pszText=(wchar_t *)names[i];
        ListView_InsertItem(app.generation,&item);
        switch(i) {
        case 0: swprintf(value,64,L"%d",app.settings.max_tokens); break;
        case 1: swprintf(value,64,L"%.2f",app.settings.temperature); break;
        case 2: swprintf(value,64,L"%.2f",app.settings.top_p); break;
        case 3: swprintf(value,64,L"%d",app.settings.context_tokens); break;
        case 4: wcscpy(value,app.settings.thinking==ThinkingAuto?L"Auto":app.settings.thinking==ThinkingOff?L"Off":L"On"); break;
        case 5: swprintf(value,64,L"%.2f",app.settings.repeat_penalty); break;
        default: swprintf(value,64,L"%.2f",app.settings.dry_multiplier); break;
        }
        ListView_SetItemText(app.generation,i,1,value);
    }
    SetWindowTextW(app.genhelp,L"Double-click a value or press Enter. Enter saves; Escape cancels.\r\n\r\nSaved per model. Context changes require unloading and loading again.\r\n\r\nThinking: Auto, Off, or On. Auto uses the template default; overrides require a supported template.\r\n\r\nRepeat: 1 = off. DRY: 0 = off.\r\n\r\nSaved chats have no exchange limit. Each request includes only the recent exchanges that fit.");
}
static void commitproperty(void)
{
    wchar_t value[64],*end;
    Generation next=app.settings;
    double number;
    int row=app.property,valid=0;
    if(row<0) return;
    app.property=-1;
    GetWindowTextW(app.value,value,64);
    number=wcstod(value,&end);
    if(row==4) {
        valid=1;
        if(!_wcsicmp(value,L"Auto")) next.thinking=ThinkingAuto;
        else if(!_wcsicmp(value,L"Off")) next.thinking=ThinkingOff;
        else if(!_wcsicmp(value,L"On")) next.thinking=ThinkingOn;
        else valid=0;
    } else if(end!=value && !*end && number>=0 && number<=1048576) {
        valid=1;
        switch(row) {
        case 0: next.max_tokens=(int)number; valid=number==next.max_tokens; break;
        case 1: next.temperature=(int)(number*100+0.5)/100.0; break;
        case 2: next.top_p=(int)(number*100+0.5)/100.0; break;
        case 3: next.context_tokens=(int)number; valid=number==next.context_tokens; break;
        case 5: next.repeat_penalty=(int)(number*100+0.5)/100.0; break;
        case 6: next.dry_multiplier=(int)(number*100+0.5)/100.0; break;
        default: valid=0;
        }
    }
    ShowWindow(app.value,SW_HIDE);
    if(!valid || !generation_valid(&next))
        note(L"Invalid setting. Response: 1-16384; context: 512-1048576; temperature: 0-2; Top P: 0.01-1; thinking: Auto/Off/On; repeat: 1-2; DRY: 0-4.");
    else if(!model_settings_save(app.config,app.modelpath,&next))
        note(L"Cannot save model settings. Select a local model and check the settings folder. Previous values retained.");
    else {
        app.settings=next;
        note(row==3?L"Context saved for this model. Unload and load again to apply it.":L"Settings saved for this model. They apply to the next message.");
    }
    generationrows();
}
static void editproperty(void)
{
    RECT r;
    wchar_t value[64];
    int row=ListView_GetNextItem(app.generation,-1,LVNI_SELECTED);
    if(app.busy || row<0 || row>6) return;
    commitproperty();
    ListView_GetSubItemRect(app.generation,row,1,LVIR_BOUNDS,&r);
    MapWindowPoints(app.generation,app.window,(POINT *)&r,2);
    ListView_GetItemText(app.generation,row,1,value,64);
    SetWindowTextW(app.value,value); app.property=row;
    MoveWindow(app.value,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);
    SetWindowPos(app.value,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW); SetFocus(app.value); SendMessageW(app.value,EM_SETSEL,0,-1);
}
static int splitter(int x,int y)
{
    PaneLayout p=workbench_layout(app.width,app.height,app.hideleft?0:app.leftsize,app.hideright?0:app.rightsize,app.hideoutput?0:app.outputsize);
    if(!app.hideoutput && y>=p.bottom-5 && y<p.bottom && x>3 && x<app.width-3) return 3;
    if(y<36 || y>=p.bottom-5) return 0;
    if(!app.hideleft && x>=p.left && x<p.left+5) return 1;
    if(!app.hideright && x>=p.rightx-5 && x<p.rightx) return 2;
    return 0;
}

static LRESULT CALLBACK
windowproc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
	switch(message) {
    case WM_LBUTTONDOWN: {
        int hit=splitter(MulDiv(GET_X_LPARAM(lp),96,app.dpi),MulDiv(GET_Y_LPARAM(lp),96,app.dpi));
        if(hit) { commitproperty(); app.dragging=hit; SetCapture(window); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if(app.dragging) {
            int x=MulDiv(GET_X_LPARAM(lp),96,app.dpi),y=MulDiv(GET_Y_LPARAM(lp),96,app.dpi);
            if(app.dragging==1) app.leftsize=layout_clamp(x,160,app.width-(app.hideright?0:app.rightsize)-336);
            else if(app.dragging==2) app.rightsize=layout_clamp(app.width-x-4,220,app.width-(app.hideleft?0:app.leftsize)-336);
            else app.outputsize=layout_clamp(app.height-y,100,app.height-420);
            layout();
        }
        return 0;
    case WM_LBUTTONUP:
        if(app.dragging) { app.dragging=0; ReleaseCapture(); savelayout(); } return 0;
    case WM_CAPTURECHANGED: app.dragging=0; return 0;
    case WM_SETCURSOR:
        if(LOWORD(lp)==HTCLIENT) {
            POINT pt; int hit;
            GetCursorPos(&pt); ScreenToClient(window,&pt);
            hit=splitter(MulDiv(pt.x,96,app.dpi),MulDiv(pt.y,96,app.dpi));
            if(hit) { SetCursor(LoadCursorW(NULL,hit==3?IDC_SIZENS:IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_SIZE:
		if(app.modules) layout();
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc=BeginPaint(window,&ps);
		paint(dc); EndPaint(window,&ps); return 0;
	}
	case WM_ERASEBKGND: return 1;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX: {
		HDC dc=(HDC)wp;
		int ispaper=(HWND)lp==app.prompt || (HWND)lp==app.model || (HWND)lp==app.port ||
		    (HWND)lp==app.activity || (HWND)lp==app.modules || (HWND)lp==app.folder || (HWND)lp==app.master;
		SetTextColor(dc,(HWND)lp==app.status?Muted:Ink);
		SetBkColor(dc,ispaper?Paper:Face);
		return (LRESULT)(ispaper?app.paper:app.face);
	}
	case WM_DRAWITEM: {
		DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT *)lp;
		if(d->CtlType==ODT_BUTTON) { drawbutton(d); return TRUE; }
		if(d->CtlType==ODT_TAB) { drawtab(d); return TRUE; }
		break;
	}
	case WM_MEASUREITEM:
		((MEASUREITEMSTRUCT *)lp)->itemHeight=(UINT)px(22); return TRUE;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lp)->ptMinTrackSize.x=px(1000);
		((MINMAXINFO *)lp)->ptMinTrackSize.y=px(680);
		return 0;
	case WM_TIMER:
		if(wp==1) pollengine();
		else if(wp==3) { KillTimer(window,3); savechat(); }
		else if(wp==2 && (app.busy || (app.process.process && app.health!=EngineReady))) {
			RECT r=rect(0,app.height-24,app.width,24); InvalidateRect(window,&r,FALSE);
		}
		return 0;
	case HealthReady: {
		Probe *p=(Probe *)lp;
		int before=app.health;
		app.checking=0;
		if(p->port==getport() && p->epoch==app.epoch) {
			app.health=p->result;
			if(app.process.process && app.health==EngineOffline) app.health=EngineLoading;
			if(app.health!=before) {
				if(app.health==EngineReady) note(app.process.process?L"Model loaded. Ready.":L"Connected to an external local engine.");
				else if(app.health==EngineOffline) note(L"Engine offline.");
				else if(app.health==EngineOther) note(L"The selected port is not a ready local engine.");
			}
			if(app.health!=before) refreshcontrols();
		}
		free(p); return 0;
	}
	case LibraryReady: modellist((ScanJob *)lp); return 0;
	case WM_NOTIFY: {
		NMHDR *n=(NMHDR *)lp;
        if(n->idFrom==IdModules && n->code==TVN_SELCHANGEDW && !app.treebusy && !app.busy) {
            LPARAM item=((NMTREEVIEWW *)lp)->itemNew.lParam;
            if(item>0 && (size_t)item<=app.store.nchats) activatechat(app.store.chats[item-1].id);
            else if(item<0 && (size_t)(-item)<=app.store.nworkspaces) {
                SendMessageW(app.workspace,CB_SETCURSEL,(WPARAM)(-item-1),0); switchworkspace();
            }
        } else if(n->idFrom==IdGeneration && (n->code==NM_DBLCLK || n->code==NM_RETURN)) editproperty();
        else if(n->idFrom==IdChatTabs && n->code==TCN_SELCHANGE) {
            int index=TabCtrl_GetCurSel(app.chattabs);
            if(index>=0 && index<app.ntabs) activatechat(app.tabs[index]);
        } else if(n->idFrom==IdLibraryTabs && n->code==TCN_SELCHANGE) {
            commitproperty(); layout(); SetFocus(n->hwndFrom);
        } else if(n->idFrom==IdModels) {
			if(n->code==LVN_ITEMCHANGED) {
				NMLISTVIEW *change=(NMLISTVIEW *)lp;
				if((change->uChanged&LVIF_STATE) && ((change->uOldState^change->uNewState)&LVIS_SELECTED)) selectmodel(0);
			}
			else if(n->code==NM_DBLCLK || n->code==NM_RETURN) selectmodel(1);
		} else if(n->idFrom==IdDetails && n->code==NM_CUSTOMDRAW) {
			NMLVCUSTOMDRAW *draw=(NMLVCUSTOMDRAW *)lp;
			if(draw->nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
			if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)) {
				draw->clrText=Ink; draw->clrTextBk=draw->iSubItem==0?RGB(243,241,230):Paper;
				return CDRF_NEWFONT;
			}
		}
		return 0;
	}
	case WM_COMMAND:
		switch(LOWORD(wp)) {
        case IdSetup: runsetup(); break;
        case IdStop: stopreply(); break;
        case IdCloseTab: closetab(); break;
        case IdValue: if(HIWORD(wp)==EN_KILLFOCUS) commitproperty(); break;
        case IdViewLeft: commitproperty(); app.hideleft=!app.hideleft; layout(); savelayout(); break;
        case IdViewRight: commitproperty(); app.hideright=!app.hideright; layout(); savelayout(); break;
        case IdViewOutput: app.hideoutput=!app.hideoutput; layout(); savelayout(); break;
        case IdResetLayout:
            commitproperty(); app.hideleft=app.hideright=app.hideoutput=0;
            app.leftsize=210; app.rightsize=280; app.outputsize=128; layout(); savelayout(); break;
        case IdGeneration:
            app.hideright=0; TabCtrl_SetCurSel(app.librarytabs,1); layout(); SetFocus(app.generation); break;
        case IdSend: submit(); break;
		case IdLoad: loadmodel(); break;
		case IdUnload:
			if(!app.busy && app.process.process) { app.epoch++; engine_stop(&app.process); app.health=EngineOffline; note(L"Model unloaded."); refreshcontrols(); }
			break;
		case IdBrowse: if(!app.busy && !app.process.process) choosefile(0); break;
		case IdEngine: if(!app.busy && !app.process.process) choosefile(1); break;
		case IdCopy: copyselection(); break;
		case IdNew:
			if(!app.busy && savechat()) {
				if(store_new(&app.store,app.store.workspaces[app.selected].id,&app.chat)) { showchat(); note(L"New chat saved."); }
				else note(app.store.error);
			}
			break;

		case IdWorkspace: if(HIWORD(wp)==CBN_SELCHANGE) switchworkspace(); break;
		case IdNewWorkspace: workspaceedit(1); break;
		case IdEditWorkspace: workspaceedit(0); break;
		case IdLibrary: app.hideright=0; TabCtrl_SetCurSel(app.librarytabs,2); layout(); SetFocus(app.folder); break;
		case IdScan: scanlibrary(); break;
		case IdPrompt:
			if(HIWORD(wp)==EN_CHANGE && !app.restoring && app.activity) {
				app.dirty=1; SetTimer(window,3,1200,NULL); InvalidateRect(window,NULL,FALSE);
			}
			break;
		case IdPort:
			if(HIWORD(wp)==EN_CHANGE && app.send) { app.epoch++; app.health=EngineOffline; refreshcontrols(); }
			break;
		case IdExit: SendMessageW(window,WM_CLOSE,0,0); break;
		case IdAbout:
			MessageBoxW(window,L"lcb-ai\nVersion 0.5.0-pre.1\n\nLocal AI processor and interface.\n\n"
			    L"Larkin Computing Bureau",L"About lcb-ai",MB_OK);
			break;
		}
		return 0;
    case StreamReady: {
        char *part=(char *)lp;
        if(app.busy && app.work) {
            wchar_t *text=wide(part);
            if(text) {
                size_t count=wcslen(text);
                if(!app.firstseconds) app.firstseconds=(double)(GetTickCount64()-app.started)/1000.0;
                if(!app.streamchars) {
                    SendMessageW(app.transcript,EM_SETSEL,(WPARAM)app.answerstart,-1);
                    SendMessageW(app.transcript,EM_REPLACESEL,FALSE,(LPARAM)L"");
                }
                if(count>=app.streamchars) appendstyle(text+app.streamchars,Ink,0,10);
                app.streamchars=count;
                SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
            }
            free(text);
        }
        free(part); return 0;
    }
    case BudgetReady: {
        Work *w=(Work *)lp;
        wchar_t summary[320];
        app.budget=w->budget;
        swprintf(summary,320,L"Context: %d prompt + %d reserved reply + 32 safety / %d tokens. %zu older exchanges excluded; all remain saved. Thinking switch: %ls.",
            w->budget.prompt_tokens,w->budget.response_tokens,w->budget.context_tokens,w->budget.omitted_messages/2,
            w->budget.thinking_supported?L"supported":L"not advertised");
        note(summary);
        return 0;
    }
    case ReplyReady: received((Work *)lp); return 0;
	case WM_CLOSE:
        if(app.busy) { stopreply(); note(L"Stopping. Close the window again after the partial reply is saved."); return 0; }
        commitproperty(); savelayout();
		if(!savechat()) { MessageBoxW(window,L"The current chat could not be saved. The window will stay open.",L"lcb-ai",MB_OK|MB_ICONERROR); return 0; }
		InterlockedExchange(&app.scancancel,1);
		engine_stop(&app.process); DestroyWindow(window); return 0;
	case WM_DESTROY: KillTimer(window,1); KillTimer(window,2); PostQuitMessage(0); return 0;
	}
	return DefWindowProcW(window,message,wp,lp);
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
	WNDCLASSW wc={0};
	MSG msg;
	HDC dc;
	HMENU menu,file,edit,engine,settings,help,view;
	INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_TREEVIEW_CLASSES};
	TCITEMW tab={0};
	LVCOLUMNW column={0};
	LARGE_INTEGER start,finish,frequency;
	wchar_t timing[128];
	DWORD corner=1,caption=RGB(49,99,161),text=RGB(255,255,255);
	(void)previous; (void)command;
	QueryPerformanceCounter(&start); QueryPerformanceFrequency(&frequency);
	InitCommonControlsEx(&common);
	SetProcessDPIAware();
	dc=GetDC(NULL); app.dpi=GetDeviceCaps(dc,LOGPIXELSY); ReleaseDC(NULL,dc);
	app.face=CreateSolidBrush(Face); app.paper=CreateSolidBrush(Paper);
	app.rich=LoadLibraryW(L"Msftedit.dll");
	if(!app.rich) return 1;
	readconfig();
	if(!openstore()) {
		MessageBoxW(NULL,app.store.error[0]?app.store.error:L"Cannot open local storage.",L"lcb-ai",MB_OK|MB_ICONERROR);
		store_close(&app.store); return 1;
	}
    {
        unsigned i,count=GetPrivateProfileIntW(L"session",L"tabs",0,app.config);
        if(count>32) count=32;
        for(i=0;i<count;i++) {
            wchar_t key[32],id[33]; char narrow[33]; size_t j;
            swprintf(key,32,L"tab%u",i);
            GetPrivateProfileStringW(L"session",key,L"",id,33,app.config);
            if(!WideCharToMultiByte(CP_UTF8,0,id,-1,narrow,33,NULL,NULL)) continue;
            for(j=0;j<app.store.nchats;j++) if(!strcmp(narrow,app.store.chats[j].id)) break;
            if(j<app.store.nchats) strcpy(app.tabs[app.ntabs++],narrow);
        }
    }
    wc.lpfnWndProc=windowproc; wc.hInstance=instance;
	wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
	wc.hbrBackground=app.face; wc.lpszClassName=L"LCBCommunicator";
	if(!RegisterClassW(&wc)) return 1;
	app.normal=CreateFontW(-px(12),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");
	app.fixed=CreateFontW(-px(12),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
	app.heading=CreateFontW(-px(12),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");
	app.icons=classic_icons(app.dpi); app.activepane=1; app.property=-1;
	view=CreatePopupMenu(); menu=CreateMenu(); file=CreatePopupMenu(); edit=CreatePopupMenu(); engine=CreatePopupMenu(); settings=CreatePopupMenu(); help=CreatePopupMenu();
	AppendMenuW(file,MF_STRING,IdNew,L"&New conversation\tCtrl+N");
	AppendMenuW(file,MF_STRING,IdNewWorkspace,L"New &workspace...");
	AppendMenuW(file,MF_STRING,IdEditWorkspace,L"Workspace &settings...");
	AppendMenuW(file,MF_SEPARATOR,0,NULL); AppendMenuW(file,MF_STRING,IdExit,L"E&xit");
    AppendMenuW(file,MF_STRING,IdCloseTab,L"Close conversation tab\tCtrl+W");
    AppendMenuW(view,MF_STRING,IdViewLeft,L"Show / hide &Workspace Explorer");
    AppendMenuW(view,MF_STRING,IdViewRight,L"Show / hide &Properties");
    AppendMenuW(view,MF_STRING,IdViewOutput,L"Show / hide &Output");
    AppendMenuW(view,MF_SEPARATOR,0,NULL);
    AppendMenuW(view,MF_STRING,IdResetLayout,L"&Reset layout");
    AppendMenuW(engine,MF_STRING,IdStop,L"&Stop generation\tEsc");
    AppendMenuW(settings,MF_STRING,IdGeneration,L"&Generation properties");
    AppendMenuW(settings,MF_STRING,IdSetup,L"&Setup...");
    AppendMenuW(edit,MF_STRING,IdCopy,L"&Copy\tCtrl+C");
	AppendMenuW(engine,MF_STRING,IdBrowse,L"Select &model...");
	AppendMenuW(engine,MF_STRING,IdEngine,L"Select &engine...");
	AppendMenuW(engine,MF_SEPARATOR,0,NULL);
	AppendMenuW(engine,MF_STRING,IdLoad,L"&Load model");
	AppendMenuW(engine,MF_STRING,IdUnload,L"&Unload model");
	AppendMenuW(settings,MF_STRING,IdLibrary,L"Model &library...");
	AppendMenuW(settings,MF_STRING,IdEditWorkspace,L"&Workspace...");
	AppendMenuW(help,MF_STRING,IdAbout,L"&About lcb-ai");
	AppendMenuW(menu,MF_POPUP,(UINT_PTR)file,L"&File");
	AppendMenuW(menu,MF_POPUP,(UINT_PTR)edit,L"&Edit");
	AppendMenuW(menu,MF_POPUP,(UINT_PTR)view,L"&View");
	AppendMenuW(menu,MF_POPUP,(UINT_PTR)engine,L"&Engine");
	AppendMenuW(menu,MF_POPUP,(UINT_PTR)settings,L"&Settings");
	AppendMenuW(menu,MF_POPUP,(UINT_PTR)help,L"&Help");
	app.window=CreateWindowW(wc.lpszClassName,L"lcb-ai",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
	    CW_USEDEFAULT,CW_USEDEFAULT,px(1240),px(820),NULL,menu,instance,NULL);
	if(!app.window) return 1;
	DwmSetWindowAttribute(app.window,33,&corner,sizeof(corner));
	DwmSetWindowAttribute(app.window,35,&caption,sizeof(caption));
	DwmSetWindowAttribute(app.window,36,&text,sizeof(text));
	app.fresh=control(L"BUTTON",L"&New",WS_TABSTOP|BS_OWNERDRAW,IdNew);
	app.load=control(L"BUTTON",L"&Load",WS_TABSTOP|BS_OWNERDRAW,IdLoad);
	app.unload=control(L"BUTTON",L"&Unload",WS_TABSTOP|BS_OWNERDRAW,IdUnload);
	app.engine=control(L"BUTTON",L"&Engine...",WS_TABSTOP|BS_OWNERDRAW,IdEngine);
	app.status=control(L"STATIC",L"Select a model to begin.",SS_LEFT,0);
	app.workspace=control(L"COMBOBOX",L"Workspace",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,IdWorkspace);
	app.newworkspace=control(L"BUTTON",L"New...",WS_TABSTOP|BS_OWNERDRAW,IdNewWorkspace);
	app.editworkspace=control(L"BUTTON",L"Edit...",WS_TABSTOP|BS_OWNERDRAW,IdEditWorkspace);
	app.modules=control(WC_TREEVIEWW,L"Workspace Explorer",WS_TABSTOP|TVS_HASBUTTONS|TVS_HASLINES|TVS_LINESATROOT|TVS_SHOWSELALWAYS,IdModules);
    TreeView_SetImageList(app.modules,app.icons,TVSIL_NORMAL);
    TreeView_SetBkColor(app.modules,Paper); TreeView_SetTextColor(app.modules,Ink);
    TreeView_SetItemHeight(app.modules,px(21));
	app.transcript=control(MSFTEDIT_CLASS,L"",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0);
	SendMessageW(app.transcript,EM_SETBKGNDCOLOR,0,Paper);
	SendMessageW(app.transcript,EM_EXLIMITTEXT,0,0x7ffffffe);
	SendMessageW(app.transcript,EM_AUTOURLDETECT,0,0);
	app.prompt=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,IdPrompt);
	SendMessageW(app.prompt,EM_SETLIMITTEXT,LcbMaxPrompt/3,0);
	SendMessageW(app.prompt,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(px(7),px(7)));
	app.stop=control(L"BUTTON",L"Stop",WS_TABSTOP|BS_OWNERDRAW,IdStop);
    app.closetab=control(L"BUTTON",L"x",WS_TABSTOP|BS_OWNERDRAW,IdCloseTab);
    app.send=control(L"BUTTON",L"&Send",WS_TABSTOP|BS_OWNERDRAW,IdSend);
	app.model=control(L"EDIT",basenamew(app.modelpath),WS_TABSTOP|WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,0);
	app.browse=control(L"BUTTON",L"&Model...",WS_TABSTOP|BS_OWNERDRAW,IdBrowse);
	app.port=control(L"EDIT",L"8080",WS_TABSTOP|WS_BORDER|ES_NUMBER,IdPort);
	SendMessageW(app.port,EM_SETLIMITTEXT,5,0);
	app.activity=control(L"EDIT",L"",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0);
	SendMessageW(app.activity,WM_SETFONT,(WPARAM)app.fixed,TRUE);
	app.chattabs=control(WC_TABCONTROLW,L"Conversation views",WS_TABSTOP|TCS_OWNERDRAWFIXED|TCS_FIXEDWIDTH,IdChatTabs);
	TabCtrl_SetPadding(app.chattabs,px(8),px(3));
    TabCtrl_SetItemSize(app.chattabs,px(148),px(23));
	tab.mask=TCIF_TEXT;
	app.master=control(L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,0);
	app.librarytabs=control(WC_TABCONTROLW,L"Library views",WS_TABSTOP|TCS_OWNERDRAWFIXED|TCS_FIXEDWIDTH,IdLibraryTabs);
	TabCtrl_SetPadding(app.librarytabs,px(7),px(3));
    TabCtrl_SetItemSize(app.librarytabs,px(87),px(23));
	tab.pszText=L"Models"; TabCtrl_InsertItem(app.librarytabs,0,&tab);
	tab.pszText=L"Generation"; TabCtrl_InsertItem(app.librarytabs,1,&tab);
	tab.pszText=L"Library"; TabCtrl_InsertItem(app.librarytabs,2,&tab);
	app.models=control(WC_LISTVIEWW,L"Local models",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,IdModels);
	if(app.icons) ListView_SetImageList(app.models,app.icons,LVSIL_SMALL);
	ListView_SetExtendedListViewStyle(app.models,LVS_EX_FULLROWSELECT|LVS_EX_LABELTIP);
	ListView_SetBkColor(app.models,Paper); ListView_SetTextBkColor(app.models,Paper); ListView_SetTextColor(app.models,Ink);
	column.mask=LVCF_TEXT|LVCF_WIDTH; column.pszText=L"Name"; column.cx=px(174); ListView_InsertColumn(app.models,0,&column);
	column.pszText=L"Size"; column.cx=px(68); ListView_InsertColumn(app.models,1,&column);
	app.details=control(WC_LISTVIEWW,L"Model properties",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_NOCOLUMNHEADER,IdDetails);
	ListView_SetExtendedListViewStyle(app.details,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_LABELTIP);
	ListView_SetBkColor(app.details,Paper); ListView_SetTextBkColor(app.details,Paper); ListView_SetTextColor(app.details,Ink);
	column.pszText=L"Property"; column.cx=px(88); ListView_InsertColumn(app.details,0,&column);
	column.pszText=L"Value"; column.cx=px(130); ListView_InsertColumn(app.details,1,&column);
    modelproperties(NULL);
    app.generation=control(WC_LISTVIEWW,L"Generation properties",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,IdGeneration);
    ListView_SetExtendedListViewStyle(app.generation,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    column.pszText=L"Property"; column.cx=px(124); ListView_InsertColumn(app.generation,0,&column);
    column.pszText=L"Value"; column.cx=px(120); ListView_InsertColumn(app.generation,1,&column);
    app.value=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,IdValue);
    SendMessageW(app.value,EM_SETLIMITTEXT,32,0); ShowWindow(app.value,SW_HIDE);
    app.genhelp=control(L"STATIC",L"",0,0);
    generationrows();
    app.folderlabel=control(L"STATIC",L"Model folder",0,0);
	app.folder=control(L"EDIT",app.libraryfolder,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,IdFolder);
	SendMessageW(app.folder,EM_SETLIMITTEXT,MAX_PATH-1,0);
	app.recursive=control(L"BUTTON",L"Include subfolders",WS_TABSTOP|BS_AUTOCHECKBOX,IdRecursive);
	SendMessageW(app.recursive,BM_SETCHECK,app.recursivevalue?BST_CHECKED:BST_UNCHECKED,0);
	app.scan=control(L"BUTTON",L"Save and scan",WS_TABSTOP|BS_OWNERDRAW,IdScan);
	if(!app.modules||!app.transcript||!app.prompt||!app.send||!app.port||!app.activity) return 1;
	tooltips(); layout(); listworkspaces(); showchat(); refreshcontrols(); note(L"Workspace ready. Select a model, then Load.");
	if(app.store.skipped) note(L"Some saved files could not be loaded. They were left unchanged in the application data folder.");
	SetTimer(app.window,1,2000,NULL); SetTimer(app.window,2,250,NULL);
	ShowWindow(app.window,show); UpdateWindow(app.window); SetFocus(app.prompt);
	QueryPerformanceCounter(&finish);
	swprintf(timing,128,L"Interface ready in %.1f ms.",1000.0*(double)(finish.QuadPart-start.QuadPart)/(double)frequency.QuadPart);
	note(timing);
	if(app.libraryfolder[0]) scanlibrary();
	pollengine();
	if(!setup_paths_ready(app.enginepath,app.modelpath)) PostMessageW(app.window,WM_COMMAND,IdSetup,0);
	while(GetMessageW(&msg,NULL,0,0)>0) {
        if(msg.message==WM_KEYDOWN && GetFocus()==app.value) {
            if(msg.wParam==VK_RETURN) { commitproperty(); SetFocus(app.generation); continue; }
            if(msg.wParam==VK_ESCAPE) { app.property=-1; ShowWindow(app.value,SW_HIDE); SetFocus(app.generation); continue; }
        }
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && app.busy) { stopreply(); continue; }
        if(msg.message==WM_KEYDOWN && GetFocus()==app.generation && msg.wParam==VK_RETURN) { editproperty(); continue; }
        if(msg.message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)) {
            if(msg.wParam=='A') {
                wchar_t kind[32]; HWND focus=GetFocus();
                GetClassNameW(focus,kind,32);
                if(!wcscmp(kind,L"Edit")) { SendMessageW(focus,EM_SETSEL,0,-1); continue; }
            }
            if(msg.wParam=='W') { closetab(); continue; }
            if(msg.wParam==VK_TAB && app.ntabs && !app.busy) {
                int index=TabCtrl_GetCurSel(app.chattabs),step=(GetKeyState(VK_SHIFT)&0x8000)?app.ntabs-1:1;
                activatechat(app.tabs[(index+step)%app.ntabs]); continue;
            }
			if(msg.wParam=='C' && GetFocus()==app.details) { copyselection(); continue; }
			if(msg.wParam==VK_RETURN) { submit(); continue; }
			if(msg.wParam=='N') { SendMessageW(app.window,WM_COMMAND,IdNew,0); continue; }
		}
		if(!IsDialogMessageW(app.window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	}
	engine_stop(&app.process);
	chat_clear(&app.chat); store_close(&app.store);
	free(app.library);
	if(app.icons) ImageList_Destroy(app.icons);
	DeleteObject(app.normal); DeleteObject(app.fixed); DeleteObject(app.heading);
	DeleteObject(app.face); DeleteObject(app.paper); FreeLibrary(app.rich);
	return 0;
}

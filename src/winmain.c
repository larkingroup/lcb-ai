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
#include "markdown.h"
#include "cJSON.h"
#include <windowsx.h>
#include <shellapi.h>

enum { IdModules = 100, IdPrompt, IdSend, IdNew, IdPort, IdExit, IdAbout,
	IdLoad, IdUnload, IdBrowse, IdEngine, IdClear, IdCopy,
	IdWorkspace, IdNewWorkspace, IdEditWorkspace, IdLibrary, IdScan, IdFolder,
	IdRecursive, IdModels, IdChatTabs, IdLibraryTabs, IdDetails,
	IdStop, IdCloseTab, IdGeneration, IdValue, IdViewLeft, IdViewRight, IdViewOutput, IdResetLayout, IdSetup,
	IdExchange, IdRetry, IdResend, IdDeleteChat, IdOpenChat, IdEngineArguments, IdOutputTabs,
    IdLatest, IdViewChats, IdChatSearch, IdSidebarMenu, IdRename, IdPin, IdFindChat, IdInsertText, IdShortcuts,
    IdMoveFirst = 3000,
	ReplyReady = WM_APP + 1, HealthReady, LibraryReady, StreamReady, BudgetReady, SettingsReady, TelemetryReady,
    TitleReady };

#define Paper RGB(255,255,255)
#define Face RGB(236,233,216)
#define Light RGB(250,249,241)
#define Shadow RGB(153,154,145)
#define Ink RGB(47,55,58)
#define Muted RGB(103,113,115)
#define ThinkingInk RGB(128,133,137)
#define Blue RGB(192,211,224)
#define BlueInk RGB(49,76,96)

typedef struct Work Work;
typedef struct StreamSnapshot StreamSnapshot;
enum { SidebarSection, SidebarWorkspace, SidebarChat, SidebarEmpty };
typedef struct SidebarRow { int kind, index, indented, expanded; long long when; } SidebarRow;
typedef struct RecentChat { size_t index; ULONGLONG modified; } RecentChat;
typedef struct ThinkingSection { LONG start,end; size_t turn; } ThinkingSection;
struct Work {
	HWND target;
	char *request;
	char *prompt;
	char *answer, *reasoning;
	char error[256];
	unsigned short port;
	int ok;
	HANDLE cancel;
	const Conversation *conversation;
	Conversation history; /* A private copy, so the open chat can change. */
	char chat[33];
	char *instruction;
	Generation settings;
	ContextBudget budget;
	ReplyReport report;
	wchar_t modelpath[MAX_PATH];
};

typedef struct SettingsWork {
    HWND window;
    unsigned epoch;
    wchar_t model[MAX_PATH], config[MAX_PATH];
    Generation settings;
    ModelInfo info;
    int model_ok;
} SettingsWork;

static struct {
	HWND window, modules, transcript, prompt, send, fresh;
	HWND status, port, load, unload, browse, engine, model, activity, outputtabs, progress;
    HWND latest, explorertoggle, chatstoggle, speed, chatsearch, sidebarmenu;
    SidebarRow *sidebar;
    int nsidebar, capsidebar, sidebarhover;
    wchar_t sidebartip[StoreName];
	HWND workspace, newworkspace, editworkspace;
	HWND chattabs, librarytabs, models, folder, recursive, scan, details, master, folderlabel;
	HWND stop, closetab, generation, value, genhelp;
	HWND exchange, retry, resend;
	size_t turn;
    Generation settings;
    ContextBudget budget;
    HANDLE cancel;
    Work *work;
    int property, sidebarbusy, dragging, leftsize, rightsize, outputsize;
    int settings_pending;
    int model_ok;
    ModelInfo model_info;
    unsigned settings_epoch;
    int answerstart;
    int hideleft, hideright, hideoutput, hidechats, followchat;
    StreamSnapshot *live;
    ThinkingSection *sections;
    unsigned char *thinking_closed;
    size_t nsections, thinking_capacity;
    char tabs[32][33];
    int ntabs;
    double firstseconds;
    Library *library;
    int sortcolumn, sortdescending;
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
	wchar_t extra_arguments[2048], phase[160];
	wchar_t activitytext[8192];
	ULONGLONG started, loadstarted;
	double lastseconds;
    int pendingsend;
    HWND renamer;
    char renaming[33];
    HANDLE titlecancel;
    char streamchat[33]; /* The chat the running reply belongs to. */
} app;


static void layout(void);
static void refreshcontrols(void);
static void refreshtitle(void);
static void pollengine(void);
static int savechat(void);
static void listchats(void);
static void whenlabel(long long then,long long now,wchar_t *out,size_t n);
static void listworkspaces(void);
static void sidebaractivate(int row,int menu);
static int sidebarchat(int row,char id[33]);
static void sidebaroptions(void);
static void showchat(void);
static void render_stream(void);
static void render_pending(void);
static int streaminghere(void);
static void synctabs(void);
static void opentab(void);
static void generationrows(void);
static void loadsettings(void);
static void commitproperty(void);
static void stopreply(void);
static void closetab(void);
static void activatechat(const char *id);
static void beginrename(const char *id);
static void quicktitle(const char *text,char out[StoreName]);
static void suggesttitle(const char *id,const char *question,const char *answer,unsigned short port);
static void inserttext(void);
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
		propertyrow(8,L"Engine requirement",model->runtime_requirement?model_runtime_description(model):L"No special requirement detected (not a compatibility guarantee)");
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
    GetPrivateProfileStringW(L"engine", L"extra_arguments", L"", app.extra_arguments, 2048, app.config);
	GetPrivateProfileStringW(L"library", L"folder", L"", app.libraryfolder, MAX_PATH, app.config);
	app.recursivevalue=GetPrivateProfileIntW(L"library",L"recursive",1,app.config)!=0;
    app.leftsize=(int)GetPrivateProfileIntW(L"layout",L"left",244,app.config);
    app.rightsize=(int)GetPrivateProfileIntW(L"layout",L"right",280,app.config);
    app.outputsize=(int)GetPrivateProfileIntW(L"layout",L"output",128,app.config);
    app.hideleft=GetPrivateProfileIntW(L"layout",L"hide_left",0,app.config)!=0;
    app.hideright=GetPrivateProfileIntW(L"layout",L"hide_right",0,app.config)!=0;
    app.hideoutput=GetPrivateProfileIntW(L"layout",L"hide_output",0,app.config)!=0;
    app.hidechats=GetPrivateProfileIntW(L"layout",L"hide_chats",0,app.config)!=0;
    app.followchat=1;
    app.settings=generation_defaults();
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

enum { CompatibilityChoose = 201, CompatibilityGuide, CompatibilityTry, CompatibilityLog };

static int compatibility_dialog(int failure)
{
    TASKDIALOGCONFIG dialog={0};
    TASKDIALOG_BUTTON buttons[4];
    wchar_t log[16385], details[4096], viewer[MAX_PATH], argument[MAX_PATH+3];
    const wchar_t *excerpt;
    const wchar_t *requirement=model_runtime_description(&app.model_info);
    int count=0, selected=IDCANCEL;
    buttons[count++]=(TASKDIALOG_BUTTON){CompatibilityChoose,L"Choose engine..."};
    if(*requirement) buttons[count++]=(TASKDIALOG_BUTTON){CompatibilityGuide,L"Official PrismML setup guide"};
    if(!failure) buttons[count++]=(TASKDIALOG_BUTTON){CompatibilityTry,L"Try selected engine"};
    else if(app.process.logpath[0]) buttons[count++]=(TASKDIALOG_BUTTON){CompatibilityLog,L"Open engine log"};
    dialog.cbSize=sizeof(dialog); dialog.hwndParent=app.window;
    dialog.dwFlags=TDF_SIZE_TO_CONTENT|TDF_ALLOW_DIALOG_CANCELLATION;
    dialog.dwCommonButtons=TDCBF_CANCEL_BUTTON; dialog.nDefaultButton=IDCANCEL;
    dialog.pszWindowTitle=L"LCB-AI model compatibility";
    dialog.pszMainIcon=TD_WARNING_ICON;
    dialog.pszMainInstruction=failure?L"The engine stopped":L"This model needs a compatible engine";
    if(failure) {
        engine_read_log(&app.process,log,16385);
        dialog.pszContent=engine_failure_advice(log);
        /* TaskDialog's expanded text does not scroll; keep the complete log in
         * the file and limit the on-screen excerpt to eight short lines. */
        excerpt=log+wcslen(log);
        for(int lines=0; excerpt>log && log+wcslen(log)-excerpt<700; ) {
            if(*--excerpt==L'\n' && ++lines==8) { excerpt++; break; }
        }
        swprintf(details,4096,L"%ls\n\nExit code: %lu\nEngine: %ls\nModel: %ls\nLog: %ls\n\n%ls",
            requirement,(unsigned long)app.process.exitcode,app.enginepath,app.modelpath,app.process.logpath,excerpt);
    } else {
        dialog.pszContent=requirement;
        swprintf(details,4096,L"Engine: %ls\nModel: %ls\nGuide: %ls",app.enginepath,app.modelpath,runtime_setup_url());
    }
    dialog.pszExpandedInformation=details; dialog.pszExpandedControlText=L"Hide details";
    dialog.pszCollapsedControlText=L"Show engine and model details";
    dialog.pszFooter=failure?(*requirement?requirement:L"The log contains the engine's actual diagnostics. Your model and chats were left unchanged."):
        L"Get the appropriate Windows llama-server from the official guide, then choose its executable. "
        L"An executable's name does not prove support. Try selected engine only if your build supports this format. "
        L"Renaming the model or downloading a projector cannot add engine support.";
    dialog.cButtons=(UINT)count; dialog.pButtons=buttons;
    if(FAILED(TaskDialogIndirect(&dialog,&selected,NULL,NULL))) {
        MessageBoxW(app.window,dialog.pszContent,L"Model compatibility - use Choose engine to select a compatible build",MB_OK|MB_ICONWARNING);
        return 0;
    }
    if(selected==CompatibilityChoose) choosefile(1);
    else if(selected==CompatibilityGuide) ShellExecuteW(app.window,L"open",runtime_setup_url(),NULL,NULL,SW_SHOWNORMAL);
    else if(selected==CompatibilityLog) {
        UINT length=GetSystemDirectoryW(viewer,MAX_PATH);
        if(length && length<MAX_PATH-13) {
            wcscat(viewer,L"\\notepad.exe");
            swprintf(argument,MAX_PATH+3,L"\"%ls\"",app.process.logpath);
            ShellExecuteW(app.window,L"open",viewer,argument,NULL,SW_SHOWNORMAL);
        }
    }
    return !failure && selected==CompatibilityTry;
}

static void
loadmodel(void)
{
	wchar_t error[256];
	unsigned short port = getport();
	if(app.busy || app.process.process || app.settings_pending) return;
	if(!port) { note(L"Enter a port from 1 to 65535."); return; }
	if(!setup_paths_ready(app.enginepath,app.modelpath) && !runsetup()) return;
	if(app.settings_pending) return;
	commitproperty();
	if(app.settings_pending) return;
	if(!app.model_ok) {
        MessageBoxW(app.window,L"Cannot read the GGUF header or tensor index. Check file access, missing shards, and download completeness. The file was left unchanged.",L"Model inspection failed",MB_OK|MB_ICONWARNING);
        return;
    }
    if(app.model_info.projector) { note(L"This is a projector companion. Select the main model GGUF instead."); return; }
    if(app.model_info.runtime_requirement && !compatibility_dialog(0)) {
        note(L"Load deferred. Select an engine with the required model support."); return;
    }
    if(!engine_start_arguments(&app.process, app.enginepath, app.modelpath, port, app.settings.context_tokens, app.extra_arguments, error, 256)) { note(error); return; }
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
	if(engine_exited(&app.process)) {
        wchar_t log[16385];
        app.epoch++; app.health=EngineOffline;
        engine_read_log(&app.process,log,16385);
        note(engine_failure_advice(log)); refreshcontrols(); compatibility_dialog(1);
    }
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
	format.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_SIZE | CFM_FACE | CFM_BACKCOLOR;
	format.crBackColor = Paper;
	format.crTextColor = color; format.dwEffects = bold ? CFE_BOLD : 0;
	format.yHeight = points * 20; wcscpy(format.szFaceName, L"Tahoma");
	SendMessageW(app.transcript, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
	SendMessage(app.transcript, EM_REPLACESEL, FALSE, (LPARAM)s);
}

static void paragraph(int indent)
{
    PARAFORMAT2 format={0};
    format.cbSize=sizeof(format);
    format.dwMask=PFM_STARTINDENT|PFM_OFFSET|PFM_RIGHTINDENT|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;
    format.dxStartIndent=indent*15;
    format.bLineSpacingRule=5; format.dyLineSpacing=22;
    SendMessageW(app.transcript,EM_SETSEL,(WPARAM)-1,-1);
    SendMessageW(app.transcript,EM_SETPARAFORMAT,0,(LPARAM)&format);
}

static void appendspeaker(int assistant)
{
    paragraph(0);
    appendstyle(assistant?L"Assistant says:\r\n":L"You say:\r\n",assistant?BlueInk:Muted,0,9);
    paragraph(8);
}

static void markdownspan(void *context,const wchar_t *text,unsigned style)
{
    CHARFORMAT2W format={0};
    format.cbSize=sizeof(format);
    format.dwMask=CFM_COLOR|CFM_BOLD|CFM_ITALIC|CFM_SIZE|CFM_FACE|CFM_BACKCOLOR;
    format.dwEffects=(style&MdBold?CFE_BOLD:0)|(style&MdItalic?CFE_ITALIC:0);
    format.crTextColor=context?ThinkingInk:style&MdQuote?Muted:Ink;
    format.crBackColor=style&MdCode?RGB(243,241,230):Paper;
    format.yHeight=(context?9:10)*20;
    wcscpy(format.szFaceName,style&MdCode?L"Consolas":L"Tahoma");
    SendMessageW(app.transcript,EM_SETSEL,(WPARAM)-1,-1);
    SendMessageW(app.transcript,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    SendMessageW(app.transcript,EM_REPLACESEL,FALSE,(LPARAM)text);
}

static void appendmessage(const char *content,int formatted,int thinking)
{
    wchar_t *text=wide(content);
    size_t length;
    if(!text) return;
    length=wcslen(text);
    while(length && (text[length-1]==L'\r' || text[length-1]==L'\n')) text[--length]=0;
    paragraph(8);
    if(formatted) markdown_render(text,markdownspan,thinking?(void *)1:NULL);
    else appendstyle(text,Ink,0,10);
    free(text);
}

static int CALLBACK comparemodels(LPARAM a,LPARAM b,LPARAM context)
{
    int result;
    (void)context;
    result=model_compare(&app.library->models[a],&app.library->models[b],app.sortcolumn);
    return app.sortdescending?-result:result;
}

static void sortmodels(void)
{
    int column;
    HWND header=ListView_GetHeader(app.models);
    if(app.library) ListView_SortItems(app.models,comparemodels,0);
    for(column=0;column<2;column++) {
        HDITEMW item={0}; item.mask=HDI_FORMAT;
        if(Header_GetItem(header,column,&item)) {
            item.fmt&=~(HDF_SORTUP|HDF_SORTDOWN);
            if(column==app.sortcolumn) item.fmt|=app.sortdescending?HDF_SORTDOWN:HDF_SORTUP;
            Header_SetItem(header,column,&item);
        }
    }
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
	sortmodels();
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
	LVITEMW item={0};
	ModelInfo *m;
	if(row<0 || !app.library || (unsigned)row>=app.library->count) { modelproperties(NULL); return; }
	item.mask=LVIF_PARAM; item.iItem=row;
	if(!ListView_GetItem(app.models,&item) || item.lParam<0 || (size_t)item.lParam>=app.library->count) return;
	m=&app.library->models[item.lParam];
	modelproperties(m);
	if(!use) return;
	if(m->projector) { note(L"This is a projector companion. Select a language model."); return; }
	if(app.busy || app.process.process || app.health==EngineReady) { note(L"Unload the current model before selecting another."); return; }
	commitproperty();
	wcscpy(app.modelpath,m->path); SetWindowTextW(app.model,m->name); loadsettings(); saveconfig(); note(L"Model selected. Choose Load to start it.");
}

static void followchat(int follow)
{
    app.followchat=follow;
    ShowWindow(app.latest,!follow && app.ntabs?SW_SHOW:SW_HIDE);
}
static int chatbottom(void)
{
    SCROLLINFO info={sizeof(info),SIF_ALL,0,0,0,0,0};
    if(!GetScrollInfo(app.transcript,SB_VERT,&info)) return 1;
    return info.nPos>=info.nMax-(int)info.nPage;
}
static void latest(void)
{
    followchat(1);
    SendMessageW(app.transcript,EM_SETSEL,(WPARAM)-1,-1);
    SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
}
static void reservethinking(void)
{
    size_t count=app.chat.conversation.count/2+1;
    ThinkingSection *sections;
    unsigned char *closed;
    if(count<=app.thinking_capacity) return;
    sections=realloc(app.sections,count*sizeof(*sections));
    if(!sections) return;
    app.sections=sections;
    closed=realloc(app.thinking_closed,count);
    if(!closed) return;
    memset(closed+app.thinking_capacity,1,count-app.thinking_capacity);
    app.thinking_closed=closed; app.thinking_capacity=count;
}
static void appendthinking(const char *trace,size_t turn)
{
    CHARRANGE caret;
    LONG start;
    int closed=turn<app.thinking_capacity && app.thinking_closed[turn];
    if(!trace || !trace[0]) return;
    SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&caret); start=caret.cpMax;
    paragraph(8);
    appendstyle(closed?L"[+] Thinking\r\n":L"[-] Thinking\r\n",ThinkingInk,1,9);
    SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&caret);
    if(app.nsections<app.thinking_capacity) {
        ThinkingSection *section=&app.sections[app.nsections++];
        section->start=start; section->end=caret.cpMax; section->turn=turn;
    }
    if(!closed) {
        appendmessage(trace,1,1);
        appendstyle(L"\r\n\r\n",ThinkingInk,0,9);
    }
}

static void
render(void)
{
	Conversation *c = &app.chat.conversation;
	wchar_t *text;
	size_t i;
    POINT scroll;
    CHARRANGE selection;
    SendMessageW(app.transcript,EM_GETSCROLLPOS,0,(LPARAM)&scroll);
    SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&selection);
    SendMessageW(app.transcript,WM_SETREDRAW,FALSE,0);
    reservethinking(); app.nsections=0;
	SendMessageW(app.exchange,CB_RESETCONTENT,0,0);
	SetWindowText(app.transcript, L"");
	if(!app.ntabs) {
		refreshcontrols();
        SendMessageW(app.transcript,WM_SETREDRAW,TRUE,0);
        InvalidateRect(app.transcript,NULL,FALSE); return;
	}
	for(i = 0; i < c->count; i++) {
		if(i%2==0) {
			wchar_t item[128];
			CHARRANGE caret;
			LRESULT row;
			size_t j;
			text=wide(c->messages[i].text);
			swprintf(item,128,L"Exchange %zu: %.80ls",i/2+1,text?text:L""); free(text);
			for(j=0;item[j];j++) if(item[j]==L'\r' || item[j]==L'\n' || item[j]==L'\t') item[j]=L' ';
			if(j && item[j-1]>=0xd800 && item[j-1]<=0xdbff) item[j-1]=0;
			row=SendMessageW(app.exchange,CB_ADDSTRING,0,(LPARAM)item);
			SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&caret);
			if(row>=0) SendMessageW(app.exchange,CB_SETITEMDATA,(WPARAM)row,caret.cpMax);
		}
        appendspeaker((int)(i%2));
        if(i%2 && c->messages[i].reasoning && c->messages[i].reasoning[0]) {
            appendthinking(c->messages[i].reasoning,i/2);
        }
        appendmessage(c->messages[i].text,(int)(i%2),0);
        if(i%2) {
            const wchar_t *state=c->messages[i].status==AnswerStopped?L"Stopped":
                c->messages[i].status==AnswerLength?L"Response limit reached":
                c->messages[i].status==AnswerError && !c->messages[i].error[0]?L"Interrupted":NULL;
            if(state) { appendstyle(L"\r\n",Muted,0,9); appendstyle(state,Muted,0,9); }
        }
		if(i%2 && c->messages[i].error[0]) {
			text=wide(c->messages[i].error);
			if(text) { appendstyle(L"\r\n",Muted,0,9); appendstyle(text,Muted,0,9); free(text); }
		}
        appendstyle(L"\r\n\r\n",Ink,0,10);
	}
	SendMessageW(app.exchange,CB_SETCURSEL,app.turn,0);
    if(streaminghere()) render_pending();
	refreshcontrols();
    if(app.followchat) SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
    else {
        SendMessageW(app.transcript,EM_EXSETSEL,0,(LPARAM)&selection);
        SendMessageW(app.transcript,EM_SETSCROLLPOS,0,(LPARAM)&scroll);
    }
    SendMessageW(app.transcript,WM_SETREDRAW,TRUE,0);
    InvalidateRect(app.transcript,NULL,FALSE);
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
	EnableWindow(app.send, app.ntabs && !app.busy && !app.settings_pending && !app.pendingsend &&
	    (app.health == EngineReady || app.process.process || setup_paths_ready(app.enginepath,app.modelpath)));
	EnableWindow(app.exchange,app.ntabs && !app.busy && app.chat.conversation.count>0);
	EnableWindow(app.retry,app.ntabs && !app.busy && !app.settings_pending && app.chat.conversation.count>0 && app.health==EngineReady);
	EnableWindow(app.resend,app.ntabs && !app.busy && !app.settings_pending && app.chat.conversation.count>0);
	EnableMenuItem(GetMenu(app.window),IdDeleteChat,MF_BYCOMMAND|
	    (app.ntabs && !streaminghere()?MF_ENABLED:MF_GRAYED));
	EnableMenuItem(GetMenu(app.window),IdCloseTab,MF_BYCOMMAND|
	    (app.ntabs?MF_ENABLED:MF_GRAYED));
	EnableMenuItem(GetMenu(app.window),IdRetry,MF_BYCOMMAND|
	    (app.ntabs && !app.busy && !app.settings_pending && app.chat.conversation.count>0 && app.health==EngineReady?MF_ENABLED:MF_GRAYED));
	EnableMenuItem(GetMenu(app.window),IdResend,MF_BYCOMMAND|
	    (app.ntabs && !app.busy && !app.settings_pending && app.chat.conversation.count>0?MF_ENABLED:MF_GRAYED));
    EnableWindow(app.stop,app.busy && app.cancel && WaitForSingleObject(app.cancel,0)!=WAIT_OBJECT_0);
    EnableWindow(app.chattabs,TRUE); EnableWindow(app.closetab,app.ntabs!=0);
    EnableWindow(app.generation,!app.busy && !app.settings_pending);
	/* Chats stay browsable while a reply streams into one of them. */
	EnableWindow(app.modules, TRUE);
    EnableWindow(app.chatsearch,TRUE); EnableWindow(app.sidebarmenu,TRUE);
	EnableWindow(app.workspace, TRUE);
	EnableWindow(app.newworkspace, TRUE);
	EnableWindow(app.editworkspace, TRUE);
	EnableWindow(app.fresh, TRUE);
	EnableWindow(app.port, !app.busy && !app.process.process);
	EnableWindow(app.load, !app.busy && !app.settings_pending && !app.process.process && app.health != EngineReady);
	EnableWindow(app.unload, !app.busy && app.process.process != NULL);
	EnableWindow(app.browse, !app.busy && !app.process.process);
	EnableWindow(app.engine, !app.busy && !app.process.process);
	EnableWindow(app.prompt,app.ntabs!=0);
	SendMessage(app.prompt, EM_SETREADONLY, streaminghere(), 0);
	InvalidateRect(app.window, NULL, FALSE);
}

/* Whether the running reply belongs to the chat on screen. */
static int streaminghere(void)
{
    return app.busy && app.work && !strcmp(app.streamchat,app.chat.info.id);
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
    free(w->reasoning);
	free(w->instruction);
	conversation_clear(&w->history);
	free(w);
}

struct StreamSnapshot {
    Work *source;
    int total,processed,cached,content;
    size_t tracebytes;
    GenerationTimings timings;
    char trace[];
};
static void render_stream(void)
{
    POINT scroll;
    CHARRANGE selection;
    const char *answer;
    size_t i=0;
    if(!app.live) return;
    SendMessageW(app.transcript,EM_GETSCROLLPOS,0,(LPARAM)&scroll);
    SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&selection);
    SendMessageW(app.transcript,WM_SETREDRAW,FALSE,0);
    SendMessageW(app.transcript,EM_SETSEL,(WPARAM)app.answerstart,-1);
    SendMessageW(app.transcript,EM_REPLACESEL,FALSE,(LPARAM)L"");
    while(i<app.nsections && app.sections[i].start<app.answerstart) i++;
    app.nsections=i;
    appendthinking(app.live->trace,app.chat.conversation.count/2);
    answer=app.live->trace+app.live->tracebytes;
    appendmessage(answer,1,0);
    if(app.followchat) SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
    else {
        SendMessageW(app.transcript,EM_EXSETSEL,0,(LPARAM)&selection);
        SendMessageW(app.transcript,EM_SETSCROLLPOS,0,(LPARAM)&scroll);
    }
    SendMessageW(app.transcript,WM_SETREDRAW,TRUE,0);
    InvalidateRect(app.transcript,NULL,FALSE);
}
static void render_pending(void)
{
    CHARRANGE caret;
    if(!app.chat.conversation.count) SetWindowTextW(app.transcript,L"");
    appendspeaker(0);
    appendmessage(app.work->prompt,0,0);
    appendstyle(L"\r\n\r\n",Ink,0,10);
    appendspeaker(1);
    SendMessageW(app.transcript,EM_EXGETSEL,0,(LPARAM)&caret); app.answerstart=caret.cpMax;
    if(app.live) render_stream();
    else appendstyle(L"...",Muted,0,9);
}
static void streamobserve(const StreamReply *state,void *context)
{
    Work *w=context;
    size_t size=state->reasoning_used+1;
    StreamSnapshot *snapshot=malloc(sizeof(*snapshot)+size+state->used+1);
    if(state->reasoning_used) {
        char *trace=malloc(size);
        if(trace) { memcpy(trace,state->reasoning,size); free(w->reasoning); w->reasoning=trace; }
    }
    if(!snapshot) return;
    snapshot->source=w; snapshot->total=state->progress_total;
    snapshot->processed=state->progress_processed; snapshot->cached=state->progress_cached;
    snapshot->content=state->used!=0; memcpy(snapshot->trace,state->reasoning,size);
    snapshot->tracebytes=size; snapshot->timings=state->timings;
    memcpy(snapshot->trace+size,state->text,state->used+1);
    if(!PostMessageW(w->target,TelemetryReady,0,(LPARAM)snapshot)) free(snapshot);
}
static DWORD WINAPI
complete(void *arg)
{
    Work *w=arg;
    Module module={"Workspace",w->instruction};
    if(local_prepare(w->port,w->modelpath,w->conversation,&module,w->prompt,&w->settings,
        w->cancel,&w->request,&w->budget,w->error,sizeof(w->error))) {
        PostMessageW(w->target,BudgetReady,0,(LPARAM)w);
        w->ok=local_stream_observe(w->port,w->request,w->cancel,NULL,streamobserve,w,&w->answer,&w->report,w->error,sizeof(w->error));
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
	if(!app.ntabs || app.busy || app.settings_pending) return;
	/* Send loads the model when needed and goes out once it is ready. */
	if(app.health != EngineReady) {
		if(GetWindowTextLength(app.prompt)==0) return;
		if(!app.process.process) loadmodel();
		if(app.process.process) { app.pendingsend=1; note(L"Loading model. Your message sends when it is ready."); refreshcontrols(); }
		return;
	}
	if(app.titlecancel) SetEvent(app.titlecancel);
	commitproperty();
	if(app.settings_pending) return;
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
	{
		size_t k;
		for(k=0;k<app.chat.conversation.count;k++)
			if(!conversation_add(&w->history,app.chat.conversation.messages[k].role,app.chat.conversation.messages[k].text)) {
				note(L"Cannot prepare message: out of memory."); workfree(w); return;
			}
	}
	w->conversation=&w->history; w->settings=app.settings;
	strcpy(w->chat,app.chat.info.id); strcpy(app.streamchat,app.chat.info.id);
	wcscpy(w->modelpath,app.modelpath);
	w->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!w->cancel) { workfree(w); note(L"Cannot create request."); return; }
    app.cancel=w->cancel; app.work=w; app.firstseconds=0;
    free(app.live); app.live=NULL;
    followchat(1);
    SetWindowTextW(app.speed,L"");
    app.started = GetTickCount64();
	busy(1);
	note(L"Connecting...");
	render();
    wcscpy(app.phase,L"Connecting...");
    SetWindowTextW(app.progress,app.phase);
	SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
	thread = CreateThread(NULL, 0, complete, w, 0, NULL);
	if(thread == NULL) {
		app.cancel=NULL; app.work=NULL; busy(0);
    SetWindowTextW(app.progress,w->ok==2?L"Stopped":!w->ok?L"Generation failed":L"Completed");
		render();
		SetWindowText(app.status, L"Cannot start the local request worker.");
		workfree(w);
	} else {
		CloseHandle(thread);
		app.restoring=1; SetWindowTextW(app.prompt,L""); app.restoring=0;
	}
}

static void
received(Work *w)
{
	Conversation *c = &app.chat.conversation;
    if(!w->answer && w->reasoning && w->reasoning[0]) {
        const char *placeholder="[No final answer was emitted.]";
        w->answer=malloc(strlen(placeholder)+1);
        if(w->answer) strcpy(w->answer,placeholder);
    }
	wchar_t *answer = w->answer ? wide(w->answer) : NULL;
	wchar_t *error;
	size_t before = c->count, bytes = c->bytes;
	app.cancel=NULL; app.work=NULL; busy(0);
    free(app.live); app.live=NULL;
    SetWindowTextW(app.progress,w->ok==2?L"Stopped":!w->ok?L"Generation failed":L"Completed");
	if(strcmp(w->chat,app.chat.info.id)) {
		/* The reply belongs to a chat that is not on screen: save it there. */
		Chat *chat=calloc(1,sizeof(*chat));
		int saved=0,first=0;
		if(chat && store_load(&app.store,w->chat,chat)) {
			Conversation *b=&chat->conversation;
			first=b->count==0 && !chat->info.renamed;
			if(answer && conversation_add(b,"user",w->prompt) && conversation_add(b,"assistant",w->answer)) {
				Message *m=&b->messages[b->count-1];
				m->reasoning=w->reasoning; w->reasoning=NULL;
				m->status=answer_status(w->ok,w->report.finish);
				if(!w->ok) snprintf(m->error,sizeof(m->error),"%s",w->error);
				if(first) quicktitle(w->prompt,chat->info.title);
				chat->draft[0]=0; chat->info.updated=store_now();
				saved=store_save(&app.store,chat);
			} else if(strlen(w->prompt)<=LcbMaxPrompt) {
				strcpy(chat->draft,w->prompt); store_save(&app.store,chat);
			}
		}
		if(saved) {
			wchar_t *title=wide(chat->info.title),line[320];
			swprintf(line,320,L"Reply saved to \"%ls\".",title?title:L"chat"); note(line); free(title);
			if(first && w->ok==1 && w->answer[0]) suggesttitle(w->chat,w->prompt,w->answer,w->port);
		} else note(w->ok==2?L"Generation stopped. The draft was kept in its chat.":L"The reply could not be saved to its chat.");
		if(chat) { chat_clear(chat); free(chat); }
		free(answer); workfree(w);
		listchats(); synctabs(); refreshcontrols();
		return;
	}
	if(answer != NULL && conversation_add(c, "user", w->prompt) &&

	    conversation_add(c, "assistant", w->answer)) {
		wchar_t summary[128];
		Message *message=&c->messages[c->count-1];
		message->reasoning=w->reasoning; w->reasoning=NULL;
		message->status=answer_status(w->ok,w->report.finish);
		if(!w->ok) snprintf(message->error,sizeof(message->error),"%s",w->error);
		app.turn=c->count/2-1;
		app.lastseconds = (double)(GetTickCount64()-app.started)/1000.0;
		swprintf(summary, 128, L"%ls in %.2f s. %d tokens. First text: %.2f s.", w->ok==2?L"Stopped; partial reply retained":!w->ok?L"Interrupted; partial reply retained":w->report.finish==FinishLength?L"Response limit reached":L"Reply received", app.lastseconds, w->report.tokens, app.firstseconds);
        if(w->report.timings.tokens_per_second>0) {
            wchar_t speed[64]; swprintf(speed,64,L"%.1f tok/s",w->report.timings.tokens_per_second);
            SetWindowTextW(app.speed,speed);
        } else SetWindowTextW(app.speed,L"");
		note(summary);
		SetWindowText(app.prompt, L"");
		{
			int first=before==0 && !app.chat.info.renamed;
			if(first) quicktitle(w->prompt,app.chat.info.title);
			app.chat.info.updated=store_now();
			app.dirty=1; savechat(); listchats(); synctabs();
			if(first && w->ok==1 && w->answer[0]) suggesttitle(app.chat.info.id,w->prompt,w->answer,w->port);
		}
		render();
	} else {
		while(c->count > before) { Message *m=&c->messages[--c->count]; free(m->text); free(m->reasoning); }
		c->bytes = bytes;
		{ wchar_t *draft=wide(w->prompt);
		  app.restoring=1; SetWindowTextW(app.prompt,draft?draft:L""); app.restoring=0; free(draft); }
		render();
		error = wide(w->ok==2 ? "Generation stopped. Your draft was retained." : w->error[0] ? w->error : "Cannot store answer: invalid UTF-8 or out of memory.");
		note(error ? error : L"Request failed.");
		free(error);
	}
	free(answer);
	workfree(w);
    if(app.followchat) SetFocus(app.prompt);
}

static void
repeat_exchange(int edit)
{
	Conversation *c=&app.chat.conversation;
	wchar_t *original=NULL, *input=NULL;
	char *prompt=NULL;
	int accepted=0;
	if(!app.ntabs || app.busy || app.settings_pending || app.turn>=c->count/2 || (!edit && app.health!=EngineReady) || !savechat()) return;
	if(edit) {
		original=wide(c->messages[app.turn*2].text);
		if(!original) return;
		input=calloc(LcbMaxPrompt*2+1,sizeof(*input));
		if(!input) { free(original); return; }
		if(wcslen(original)>LcbMaxPrompt*2) { free(original); free(input); note(L"This message is too long to edit."); return; }
		wcscpy(input,original);
		while(edit_message(app.window,app.normal,input,LcbMaxPrompt*2+1)) {
			prompt=utf8(input);
			if(prompt && prompt[0] && strlen(prompt)<=LcbMaxPrompt) { accepted=1; break; }
			free(prompt); prompt=NULL;
			MessageBoxW(app.window,L"Message must be valid text, up to 16 KiB.",L"lcb-ai",MB_OK);
		}
	} else {
		const char *text=c->messages[app.turn*2].text;
		prompt=malloc(strlen(text)+1);
		if(prompt) { strcpy(prompt,text); accepted=1; }
	}
	free(original); free(input);
	if(accepted) {
		if(store_branch(&app.store,&app.chat,app.turn,prompt,edit?"Edit":"Retry",&app.chat)) {
			showchat();
			note(L"Conversation opened from the selected exchange.");
			if(app.health==EngineReady) submit();
			else note(L"Draft saved. Load a model, then choose Send.");
		} else note(app.store.error);
	}
	free(prompt);
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
    HWND buttons[]={app.fresh,app.load,app.unload,app.engine,app.browse,app.stop,app.closetab,app.newworkspace,app.editworkspace,app.exchange,app.retry,app.resend,
        app.latest,app.explorertoggle,app.chatstoggle,app.speed};
    const wchar_t *texts[]={L"New conversation (Ctrl+N)",L"Load the selected local model",L"Unload the model from memory",
        L"Choose the local llama.cpp engine",L"Choose an existing GGUF file",L"Stop generation (Escape)",
        L"Close this tab; saved chat is retained (Ctrl+W)",L"Create a workspace",L"Edit workspace name and system instructions",
        L"Select an exchange to view or retry",L"Try this exchange again in a new conversation",
        L"Edit this message and continue in a new conversation",
        L"Resume following the response (End)",L"Collapse or expand Chats",
        L"Collapse or expand open chat tabs",L"Native llama.cpp generation speed, including thinking and answer tokens"};
    size_t i;
    for(i=0;tips && i<sizeof(buttons)/sizeof(buttons[0]);i++) {
        TOOLINFOW tool={0}; tool.cbSize=sizeof(tool); tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tool.hwnd=app.window; tool.uId=(UINT_PTR)buttons[i]; tool.lpszText=(wchar_t *)texts[i];
        SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tool);
    }
    if(tips) {
        TOOLINFOW tool={0}; tool.cbSize=sizeof(tool); tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tool.hwnd=app.window; tool.uId=(UINT_PTR)app.modules; tool.lpszText=LPSTR_TEXTCALLBACKW;
        SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tool);
        tool.uId=(UINT_PTR)app.sidebarmenu; tool.lpszText=L"Chat and workspace options";
        SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tool);
        tool.uId=(UINT_PTR)app.chatsearch; tool.lpszText=L"Search chat titles and workspace names";
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
    line(dc,182,5,182,26,Shadow);
    label(dc,rect(w-96,3,82,23),L"lcb-ai",BlueInk,app.heading,DT_RIGHT);
    if(!app.hideleft) {
        panel(dc,4,36,p.left-4,p.bottom-41,L"Chats",app.activepane==0,ClassicFolder);
        label(dc,rect(12,p.bottom-27,p.left-20,18),app.dirty?L"Unsaved changes":L"Saved locally",Muted,app.normal,DT_LEFT);
    }
    {
        wchar_t title[300],*name=wide(app.chat.info.title);
        swprintf(title,300,L"Conversations%ls%ls",app.hidechats?L" · ":L"",app.hidechats && name?name:L"");
        panel(dc,x,36,cw,p.bottom-41,title,app.activepane==1,ClassicDocument);
        free(name);
    }
    fill(dc,rect(x+2,p.composer,cw-4,21),Face);
    line(dc,x+2,p.composer,x+cw-2,p.composer,Shadow);
    label(dc,rect(x+8,p.composer+1,cw-240,19),L"Message · Ctrl+Enter to send",Ink,app.normal,DT_LEFT);
    if(!app.hideright) {
        panel(dc,p.rightx,36,p.right,p.bottom-41,L"Properties and Models",app.activepane==2,ClassicSettings);
        if(tab==0) {
            fill(dc,rect(p.rightx+3,p.bottom-207,p.right-6,21),Face);
            label(dc,rect(p.rightx+9,p.bottom-207,p.right-18,21),L"Model properties",Ink,app.heading,DT_LEFT);
        } else fill(dc,rect(p.rightx+3,84,p.right-6,p.bottom-94),Face);
    }
    if(!app.hideoutput) {
        panel(dc,4,p.bottom,w-8,h-p.bottom-29,L"Output",app.activepane==3,ClassicOutput);
    }
    classic_edge(dc,rect(4,h-22,206,20),0);
    classic_edge(dc,rect(213,h-22,260,20),0);
    classic_edge(dc,rect(476,h-22,w-480,20),0);
    label(dc,rect(12,h-22,190,20),app.busy?app.phase:app.health==EngineReady?L"Engine ready":app.health==EngineLoading?L"Loading model...":L"Engine offline",Ink,app.normal,DT_LEFT);
    if(app.health==EngineLoading) {
        int i,step=(int)((GetTickCount64()/125)%16);
        RECT bar=rect(219,h-18,140,12);
        fill(dc,bar,Paper); classic_edge(dc,bar,0);
        for(i=0;i<16;i++) if((i-step+16)%16<5)
            fill(dc,rect(222+i*8,h-15,6,6),RGB(38,116,61));
        if(app.loadstarted && app.process.process)
            swprintf(text,256,L"%.1f s",(double)(GetTickCount64()-app.loadstarted)/1000.0);
        else wcscpy(text,L"Loading");
        label(dc,rect(367,h-22,97,20),text,Muted,app.normal,DT_RIGHT);
    } else {
        if(app.busy) swprintf(text,256,L"Elapsed: %.1f s",(double)(GetTickCount64()-app.started)/1000.0);
        else if(app.lastseconds>0) swprintf(text,256,L"Last reply: %.2f s",app.lastseconds);
        else text[0]=0;
        label(dc,rect(221,h-22,244,20),text,Muted,app.normal,DT_LEFT);
    }
    if(app.budget.context_tokens)
        swprintf(text,256,L"%zu saved exchanges | Context: %d + %d reply / %d | %zu older excluded",
            app.chat.conversation.count/2,app.budget.prompt_tokens,app.budget.response_tokens,
            app.budget.context_tokens,app.budget.omitted_messages/2);
    else swprintf(text,256,L"127.0.0.1:%u   |   %zu saved exchanges",getport(),app.chat.conversation.count/2);
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
	case IdNew: break;
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
	if(d->CtlID==IdNew) {
        int x=MulDiv(t.left+t.right,96,app.dpi)/2,y=MulDiv(t.top+t.bottom,96,app.dpi)/2;
        line(d->hDC,x-5,y,x+6,y,disabled?Muted:Ink);
        line(d->hDC,x,y-5,x,y+6,disabled?Muted:Ink);
    } else
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

static void drawsidebar(DRAWITEMSTRUCT *d)
{
    SidebarRow *row=(SidebarRow *)d->itemData;
    RECT r=d->rcItem,t=r;
    wchar_t text[StoreName],when[32];
    int active=0,pinned=0,hover=(int)d->itemID==app.sidebarhover,inset=9;
    if(d->itemID==(UINT)-1 || !row) { fill(d->hDC,r,Light); return; }
    if(row->kind==SidebarChat && (size_t)row->index<app.store.nchats) {
        active=app.ntabs && !strcmp(app.store.chats[row->index].id,app.chat.info.id);
        pinned=app.store.chats[row->index].pinned;
    }
    fill(d->hDC,r,active?RGB(76,112,157):hover && row->kind==SidebarChat?Face:Light);
    if(active) {
        RECT stripe=r; stripe.bottom=stripe.top+px(1);
        fill(d->hDC,stripe,RGB(145,175,205));
    }
    t.right-=px(8);
    if(row->kind==SidebarChat) {
        if(hover) {
            RECT dots=r; dots.left=dots.right-px(26);
            label(d->hDC,dots,L"...",active?Paper:Muted,app.normal,DT_CENTER);
            t.right=r.right-px(30);
        } else {
            whenlabel(row->when,store_now(),when,32);
            if(when[0]) {
                RECT w=t; SIZE size; HFONT old=SelectObject(d->hDC,app.normal);
                GetTextExtentPoint32W(d->hDC,when,(int)wcslen(when),&size); SelectObject(d->hDC,old);
                label(d->hDC,w,when,active?RGB(214,226,240):Muted,app.normal,DT_RIGHT);
                t.right-=size.cx+px(8);
            }
        }
        if(pinned) {
            int x=MulDiv(r.left,96,app.dpi)+12,y=MulDiv(r.top,96,app.dpi)+14;
            COLORREF c=active?Paper:RGB(195,146,58);
            line(d->hDC,x-4,y-1,x+5,y-1,c); line(d->hDC,x-2,y-6,x-2,y-1,c);
            line(d->hDC,x+2,y-6,x+2,y-1,c); line(d->hDC,x-2,y-6,x+3,y-6,c);
            line(d->hDC,x,y-1,x,y+5,c);
            inset=22;
        }
    }
    t.left+=px(inset);
    SendMessageW(app.modules,LB_GETTEXT,d->itemID,(LPARAM)text);
    label(d->hDC,t,text,active?Paper:row->kind==SidebarSection || row->kind==SidebarEmpty?Muted:Ink,
        row->kind==SidebarSection?app.heading:app.normal,DT_LEFT);
    if((d->itemState&ODS_FOCUS) && GetFocus()==app.modules) {
        t=r; InflateRect(&t,-px(2),-px(2)); DrawFocusRect(d->hDC,&t);
    }
}

static LRESULT CALLBACK
classiccontrol(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
	(void)data;
    if(window==app.modules) {
        if(message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN) {
            POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            RECT r,client;
            LRESULT hit=SendMessageW(window,LB_ITEMFROMPOINT,0,lp);
            int row=HIWORD(hit)?-1:LOWORD(hit);
            if(row>=app.nsidebar || row<0 || SendMessageW(window,LB_GETITEMRECT,row,(LPARAM)&r)==LB_ERR || !PtInRect(&r,point)) row=-1;
            if(message==WM_MOUSEMOVE && row!=app.sidebarhover) {
                TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0};
                app.sidebarhover=row; TrackMouseEvent(&track); InvalidateRect(window,NULL,FALSE);
            } else if(message==WM_LBUTTONDOWN && row>=0) {
                if(app.sidebar[row].kind!=SidebarChat && app.sidebar[row].kind!=SidebarWorkspace) return 0;
                SetFocus(window); SendMessageW(window,LB_SETCURSEL,row,0);
                GetClientRect(window,&client);
                sidebaractivate(row,point.x>client.right-px(28));
                return 0;
            }
        } else if(message==WM_LBUTTONDBLCLK) {
            char chat[33];
            LRESULT hit=SendMessageW(window,LB_ITEMFROMPOINT,0,lp);
            if(!HIWORD(hit) && sidebarchat(LOWORD(hit),chat)) beginrename(chat);
            return 0;
        } else if(message==WM_MOUSELEAVE) {
            app.sidebarhover=-1; InvalidateRect(window,NULL,FALSE);

        } else if(message==WM_KEYDOWN) {
            int row=(int)SendMessageW(window,LB_GETCURSEL,0,0);
            if(wp==VK_UP || wp==VK_DOWN) {
                int step=wp==VK_UP?-1:1;
                for(int next=row+step;next>=0 && next<app.nsidebar;next+=step)
                    if(app.sidebar[next].kind==SidebarChat || app.sidebar[next].kind==SidebarWorkspace) {
                        SendMessageW(window,LB_SETCURSEL,next,0); return 0;
                    }
                return 0;
            }
            if(row>=0 && row<app.nsidebar) {
                SidebarRow item=app.sidebar[row];
                if(wp==VK_RETURN || (item.kind==SidebarWorkspace &&
                    ((wp==VK_RIGHT && !item.expanded) || (wp==VK_LEFT && item.expanded)))) {
                    sidebaractivate(row,0); return 0;
                }
            }
        }
    }
    if(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL) {
        POINT point; HWND target;
        GetCursorPos(&point); target=WindowFromPoint(point);
        if(target!=window && target!=app.window && IsChild(app.window,target)) {
            SendMessageW(target,message,wp,lp);
            return 0;
        }
        if(target!=window) return 0;
        if(window==app.transcript) {
            DefSubclassProc(window,message,wp,lp);
            if(message==WM_MOUSEWHEEL)
                followchat(GET_WHEEL_DELTA_WPARAM(wp)<0 && chatbottom());
            return 0;
        }
    }
    if(window==app.transcript) {
        if(message==WM_LBUTTONDOWN) {
            POINTL point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            LONG position=(LONG)SendMessageW(window,EM_CHARFROMPOS,0,(LPARAM)&point);
            size_t i;
            followchat(0);
            for(i=0;i<app.nsections;i++) {
                ThinkingSection section=app.sections[i];
                if(position>=section.start && position<section.end && section.turn<app.thinking_capacity) {
                    app.thinking_closed[section.turn]=!app.thinking_closed[section.turn];
                    if(streaminghere() && section.turn==app.chat.conversation.count/2) render_stream();
                    else render();
                    return 0;
                }
            }
        } else if(message==WM_VSCROLL || message==WM_KEYDOWN) {
            if(message==WM_KEYDOWN && wp==VK_END) { latest(); return 0; }
            if(message==WM_VSCROLL || wp==VK_PRIOR || wp==VK_NEXT ||
                wp==VK_HOME || wp==VK_UP || wp==VK_DOWN) {
                LRESULT result=DefSubclassProc(window,message,wp,lp);
                followchat(chatbottom());
                return result;
            }
        }
    }
	if(message==WM_MOUSEMOVE && (GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW &&
	    (window==app.fresh || window==app.load || window==app.unload || window==app.engine ||
	    window==app.browse || window==app.send || window==app.scan ||
	    window==app.newworkspace || window==app.editworkspace || window==app.sidebarmenu || window==app.stop || window==app.closetab || window==app.retry || window==app.resend)) {
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
		if(window==app.workspace || window==app.modules || window==app.chatsearch || window==app.sidebarmenu || window==app.fresh || window==app.newworkspace || window==app.editworkspace) pane=0;
		else if(window==app.transcript || window==app.prompt || window==app.master || window==app.chattabs || window==app.send || window==app.exchange || window==app.retry || window==app.resend) pane=1;
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
    place(app.load,6,4,79,23); place(app.unload,89,4,85,23);
    place(app.engine,190,4,78,23); place(app.browse,273,4,84,23);
    place(app.model,364,5,w-556,21); place(app.port,w-185,5,62,21);
    ShowWindow(app.master,SW_HIDE);
    place(app.fresh,left?p.left-76:7,left?38:64,22,20);
    place(app.sidebarmenu,p.left-51,38,22,20);
    /* The workspace list sits above search, as on Linux. */
    place(app.workspace,10,63,p.left-16,300);
    place(app.chatsearch,10,92,p.left-16,26);
    place(app.modules,9,124,p.left-14,p.bottom-157);
    ShowWindow(app.workspace,left?SW_SHOW:SW_HIDE);
    ShowWindow(app.modules,left?SW_SHOW:SW_HIDE);
    ShowWindow(app.fresh,left?SW_SHOW:SW_HIDE);
    ShowWindow(app.chatsearch,left?SW_SHOW:SW_HIDE);
    ShowWindow(app.sidebarmenu,left?SW_SHOW:SW_HIDE);
    ShowWindow(app.newworkspace,SW_HIDE); ShowWindow(app.editworkspace,SW_HIDE);
    place(app.explorertoggle,left?p.left-26:x+3,38,22,20);
    SetWindowTextW(app.explorertoggle,left?L"<":L">");
    place(app.chatstoggle,x+cw-25,38,22,20);
    SetWindowTextW(app.chatstoggle,app.hidechats?L"+":L"-");
    place(app.chattabs,x+2,57,cw-29,25); place(app.closetab,x+cw-25,58,22,22);
    ShowWindow(app.chattabs,app.hidechats?SW_HIDE:SW_SHOW);
    ShowWindow(app.closetab,app.hidechats?SW_HIDE:SW_SHOW);
    place(app.exchange,x+7,app.hidechats?60:85,cw-212,260);
    place(app.retry,x+cw-200,app.hidechats?60:85,62,23);
    place(app.resend,x+cw-134,app.hidechats?60:85,127,23);
    place(app.transcript,x+3,app.hidechats?87:112,cw-6,p.composer-(app.hidechats?89:114));
    place(app.speed,x+cw-224,p.composer+1,136,19);
    place(app.latest,x+cw-83,p.composer+1,80,20);
    ShowWindow(app.latest,!app.followchat && app.ntabs?SW_SHOW:SW_HIDE);
    GetClientRect(app.transcript,&margin); InflateRect(&margin,-px(12),-px(8));
    if(margin.right>margin.left+px(720)) margin.right=margin.left+px(720);
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
    place(app.generation,p.rightx+6,91,p.right-12,256);
    ListView_SetColumnWidth(app.generation,0,px(124)); ListView_SetColumnWidth(app.generation,1,px(p.right-141));
    place(app.genhelp,p.rightx+10,358,p.right-20,p.bottom-368);
    ShowWindow(app.librarytabs,right?SW_SHOW:SW_HIDE);
    ShowWindow(app.models,right && tab==0?SW_SHOW:SW_HIDE); ShowWindow(app.details,right && tab==0?SW_SHOW:SW_HIDE);
    ShowWindow(app.generation,right && tab==1?SW_SHOW:SW_HIDE); ShowWindow(app.genhelp,right && tab==1?SW_SHOW:SW_HIDE);
    ShowWindow(app.folderlabel,right && tab==2?SW_SHOW:SW_HIDE); ShowWindow(app.folder,right && tab==2?SW_SHOW:SW_HIDE);
    ShowWindow(app.recursive,right && tab==2?SW_SHOW:SW_HIDE); ShowWindow(app.scan,right && tab==2?SW_SHOW:SW_HIDE);
    place(app.outputtabs,9,p.bottom+24,270,24);
    place(app.progress,290,p.bottom+25,w-308,22);
    place(app.activity,9,p.bottom+51,w-18,h-p.bottom-86);
    ShowWindow(app.outputtabs,out?SW_SHOW:SW_HIDE);
    ShowWindow(app.progress,out?SW_SHOW:SW_HIDE);
    ShowWindow(app.activity,out?SW_SHOW:SW_HIDE); ShowWindow(app.status,SW_HIDE);
    ShowWindow(app.value,SW_HIDE); app.property=-1;
    if(app.followchat) SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
    InvalidateRect(app.window,NULL,FALSE);
}

static int
savechat(void)
{
	wchar_t *input;
	char *text;
	int n;
	if(!app.dirty || !app.ntabs) return 1;
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

static int recentcompare(const void *a,const void *b)
{
    const RecentChat *x=a,*y=b;
    if(x->modified!=y->modified) return x->modified>y->modified?-1:1;
    return strcmp(app.store.chats[x->index].id,app.store.chats[y->index].id);
}

static void sidebarrow(int kind,int index,int indented,int expanded,const wchar_t *text)
{
    LRESULT item;
    SidebarRow *row;
    wchar_t title[StoreName]; size_t i=0,j=0;
    if(app.nsidebar>=app.capsidebar) return;
    row=&app.sidebar[app.nsidebar++];
    row->kind=kind; row->index=index; row->indented=indented; row->expanded=expanded;
    while(text[i] && j<StoreName-1) {
        wchar_t c=text[i++];
        if(c==L'\r') continue;
        title[j++]=c==L'\n' || c==L'\t'?L' ':c;
    }
    title[j]=0;
    item=SendMessageW(app.modules,LB_ADDSTRING,0,(LPARAM)title);
    if(item!=LB_ERR && item!=LB_ERRSPACE) SendMessageW(app.modules,LB_SETITEMDATA,(WPARAM)item,(LPARAM)row);
}

static int sidebarmatches(const char *text,const wchar_t *query)
{
    wchar_t *value=wide(text);
    int result=value && FindStringOrdinal(FIND_FROMSTART,value,-1,query,-1,TRUE)>=0;
    free(value); return !query[0] || result;
}

/* Upper-cased message text per chat for search; rebuilt only when a chat gains turns.
 * Entries follow the chat index, so lookups stay constant time; when a delete
 * shifts the index, an entry is found by id once and swapped into place. */
typedef struct SearchText { char id[33]; size_t turns; wchar_t *text; } SearchText;
static SearchText *searchcache;
static size_t nsearchcache;

static const wchar_t *chattext(size_t index)
{
    const ChatInfo *info=&app.store.chats[index];
    size_t slot=index,i;
    Chat *chat;
    if(index>=nsearchcache) {
        size_t n=app.store.capchats>index?app.store.capchats:index+1;
        SearchText *grown=realloc(searchcache,n*sizeof(*grown));
        if(!grown) return L"";
        memset(grown+nsearchcache,0,(n-nsearchcache)*sizeof(*grown));
        searchcache=grown; nsearchcache=n;
    }
    if(strcmp(searchcache[slot].id,info->id))
        for(i=0;i<nsearchcache;i++)
            if(!strcmp(searchcache[i].id,info->id)) {
                SearchText moved=searchcache[i]; searchcache[i]=searchcache[slot]; searchcache[slot]=moved;
                break;
            }
    if(!strcmp(searchcache[slot].id,info->id) && searchcache[slot].turns==info->turns && searchcache[slot].text)
        return searchcache[slot].text;
    free(searchcache[slot].text); searchcache[slot].text=NULL;
    strcpy(searchcache[slot].id,info->id); searchcache[slot].turns=info->turns;
    chat=calloc(1,sizeof(*chat));
    if(chat && store_load(&app.store,info->id,chat)) {
        size_t n=0,k;
        char *all;
        for(k=0;k<chat->conversation.count;k++) n+=strlen(chat->conversation.messages[k].text)+1;
        all=malloc(n+1);
        if(all) {
            size_t at=0,len;
            for(k=0;k<chat->conversation.count;k++) {
                len=strlen(chat->conversation.messages[k].text);
                memcpy(all+at,chat->conversation.messages[k].text,len); at+=len; all[at++]='\n';
            }
            all[at]=0;
            searchcache[slot].text=wide(all); free(all);
            /* Folded once here so each keystroke is a plain substring scan. */
            if(searchcache[slot].text) CharUpperBuffW(searchcache[slot].text,(DWORD)wcslen(searchcache[slot].text));
        }
    }
    if(chat) { chat_clear(chat); free(chat); }
    app.store.error[0]=0;
    return searchcache[slot].text?searchcache[slot].text:L"";
}

/* Local calendar time for a Unix second, through the Win32 clock. */
static int localdate(long long unix,SYSTEMTIME *out)
{
    ULONGLONG ticks=(ULONGLONG)unix*10000000ULL+116444736000000000ULL;
    FILETIME utc={(DWORD)ticks,(DWORD)(ticks>>32)},local;
    return FileTimeToLocalFileTime(&utc,&local) && FileTimeToSystemTime(&local,out);
}

static long long localmidnight(long long now)
{
    SYSTEMTIME day; FILETIME local,utc; ULONGLONG ticks;
    if(!localdate(now,&day)) return now-now%86400;
    day.wHour=day.wMinute=day.wSecond=day.wMilliseconds=0;
    if(!SystemTimeToFileTime(&day,&local) || !LocalFileTimeToFileTime(&local,&utc)) return now-now%86400;
    ticks=((ULONGLONG)utc.dwHighDateTime<<32)|utc.dwLowDateTime;
    return (long long)((ticks-116444736000000000ULL)/10000000ULL);
}

/* Compact recency: time today, weekday this week, then date. */
static void whenlabel(long long then,long long now,wchar_t *out,size_t n)
{
    SYSTEMTIME a,b;
    static const wchar_t *days[]={L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"};
    static const wchar_t *months[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};
    out[0]=0;
    if(then<=0 || !localdate(then,&a) || !localdate(now,&b)) return;
    if(then>=localmidnight(now)) swprintf(out,n,L"%02d:%02d",a.wHour,a.wMinute);
    else if(now>=then && now-then<6*86400) swprintf(out,n,L"%ls",days[a.wDayOfWeek%7]);
    else if(a.wYear==b.wYear) swprintf(out,n,L"%ls %d",months[(a.wMonth+11)%12],a.wDay);
    else swprintf(out,n,L"%ls %d",months[(a.wMonth+11)%12],a.wYear);
}

static void chatrow(size_t chat,int *selected)
{
    wchar_t *name=wide(app.store.chats[chat].title);
    sidebarrow(SidebarChat,(int)chat,0,0,name?name:L"Chat"); free(name);
    app.sidebar[app.nsidebar-1].when=app.store.chats[chat].updated;
    if(!strcmp(app.store.chats[chat].id,app.chat.info.id) && *selected<0) *selected=app.nsidebar-1;
}

static void
listchats(void)
{
    size_t i;
    int selected=-1,top=(int)SendMessageW(app.modules,LB_GETTOPINDEX,0,0),count=0;
    wchar_t query[StoreName];
    RecentChat *recent=malloc((app.store.nchats+1)*sizeof(*recent));
    long long now=store_now();
    if(!recent) return;
    GetWindowTextW(app.chatsearch,query,StoreName);
    for(i=0;i<app.store.nchats;i++) { recent[i].index=i; recent[i].modified=(ULONGLONG)app.store.chats[i].updated; }
    qsort(recent,app.store.nchats,sizeof(*recent),recentcompare);
    app.sidebarbusy=1; app.sidebarhover=-1;
    SendMessageW(app.modules,WM_SETREDRAW,FALSE,0);
    SendMessageW(app.modules,LB_RESETCONTENT,0,0); app.nsidebar=0;
    /* Rows point into this array, so it grows only while the list is empty:
     * one row per chat plus section headings. */
    if(app.capsidebar<(int)app.store.nchats+8) {
        SidebarRow *grown=realloc(app.sidebar,(app.store.nchats+64)*sizeof(*grown));
        if(grown) { app.sidebar=grown; app.capsidebar=(int)app.store.nchats+64; }
    }
    if(!query[0]) {
        /* One workspace at a time: pinned first, then by day. */
        const char *ws=app.store.workspaces[app.selected].id;
        static const wchar_t *groups[]={L"Today",L"Yesterday",L"Previous 7 days",L"Previous 30 days",L"Older"};
        long long bounds[5];
        int group=-1,g;
        bounds[0]=localmidnight(now); bounds[1]=bounds[0]-86400;
        bounds[2]=bounds[0]-6*86400; bounds[3]=bounds[0]-29*86400; bounds[4]=-1;
        for(i=0;i<app.store.nchats;i++) {
            size_t chat=recent[i].index;
            if(strcmp(app.store.chats[chat].workspace,ws) || !app.store.chats[chat].pinned) continue;
            if(!count++) sidebarrow(SidebarSection,0,0,0,L"Pinned");
            chatrow(chat,&selected);
        }
        for(i=0;i<app.store.nchats;i++) {
            size_t chat=recent[i].index;
            if(strcmp(app.store.chats[chat].workspace,ws) || app.store.chats[chat].pinned) continue;
            for(g=0;g<4 && app.store.chats[chat].updated<bounds[g];g++);
            if(g!=group) { group=g; sidebarrow(SidebarSection,0,0,0,groups[g]); }
            chatrow(chat,&selected); count++;
        }
        if(!count) sidebarrow(SidebarEmpty,0,0,0,L"No chats yet. Ctrl+N starts one.");
    } else {
        /* Titles first, then chats whose messages match, across workspaces. */
        int pass,shown;
        wchar_t folded[StoreName];
        wcscpy(folded,query); CharUpperBuffW(folded,(DWORD)wcslen(folded));
        for(pass=0;pass<2;pass++) {
            shown=0;
            for(i=0;i<app.store.nchats;i++) {
                size_t chat=recent[i].index;
                int title=sidebarmatches(app.store.chats[chat].title,query);
                if(pass==0 ? !title : title || !wcsstr(chattext(chat),folded)) continue;
                if(!shown++) sidebarrow(SidebarSection,0,0,0,pass?L"Messages":L"Titles");
                chatrow(chat,&selected); count++;
            }
        }
        if(!count) sidebarrow(SidebarEmpty,0,0,0,L"No matches");
    }
    SendMessageW(app.modules,LB_SETCURSEL,selected,0);
    SendMessageW(app.modules,LB_SETTOPINDEX,top,0);
    SendMessageW(app.modules,WM_SETREDRAW,TRUE,0); InvalidateRect(app.modules,NULL,TRUE);
    app.sidebarbusy=0; free(recent);
}

static void
showchat(void)
{
    followchat(1);
    SetWindowTextW(app.speed,L"");
    if(app.thinking_capacity) memset(app.thinking_closed,1,app.thinking_capacity);
    memset(&app.budget,0,sizeof(app.budget));
	app.turn=app.chat.conversation.count ? app.chat.conversation.count/2-1 : 0;
	wchar_t *draft=wide(app.chat.draft), id[33];
	wchar_t *master=wide(app.store.workspaces[app.selected].prompt);
	SetWindowTextW(app.master,master?master:L""); free(master);
	app.restoring=1; SetWindowTextW(app.prompt,draft?draft:L""); app.restoring=0; free(draft);
	app.dirty=0; KillTimer(app.window,3);
	swprintf(id,33,L"%hs",app.chat.info.id);
	WritePrivateProfileStringW(L"session",L"chat",id,app.config);
	if(app.chat.info.id[0]) opentab(); else synctabs();
	listchats(); render(); layout();
}

static void activatechat(const char *id)
{
    size_t i;
    if(!strcmp(id,app.chat.info.id)) { if(!app.ntabs) showchat(); else synctabs(); return; }
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
	if(index<0 || (size_t)index>=app.store.nworkspaces) return;
	if(!savechat()) { listworkspaces(); return; }
	{
	    /* Open the workspace's most recent chat. */
	    size_t best=app.store.nchats;
	    for(i=0;i<app.store.nchats;i++)
	        if(!strcmp(app.store.chats[i].workspace,app.store.workspaces[index].id) &&
	            (best==app.store.nchats || app.store.chats[i].updated>app.store.chats[best].updated)) best=i;
	    i=best;
	}
	SetWindowTextW(app.chatsearch,L"");
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
	if(i==app.store.nchats && app.store.nchats) i=0;
	if(i<app.store.nchats) {
		if(!store_load(&app.store,app.store.chats[i].id,&app.chat)) return 0;
	} else { chat_clear(&app.chat); app.selected=0; return 1; }
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
    saveint(L"layout",L"hide_chats",app.hidechats);
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
    if(index<0 || !savechat()) return;
    if(app.ntabs==1) {
        app.ntabs=0; synctabs();
        app.sidebarbusy=1; SendMessageW(app.modules,LB_SETCURSEL,(WPARAM)-1,0); app.sidebarbusy=0;
        app.restoring=1; SetWindowTextW(app.prompt,L""); app.restoring=0;
        memset(&app.budget,0,sizeof(app.budget)); render(); return;
    }
    memmove(app.tabs+index,app.tabs+index+1,(size_t)(app.ntabs-index-1)*sizeof(app.tabs[0]));
    app.ntabs--;
    activatechat(app.tabs[index<app.ntabs?index:app.ntabs-1]);
}

/* The caller confirms first; no visible state changes on storage failure. */
static int deletechat(const char *id)
{
    char target[33];
    Chat *next=NULL;
    int active, i, j;
    size_t workspace;
    if((app.busy && !strcmp(id,app.streamchat)) || strlen(id)!=32) return 0;
    strcpy(target,id); active=!strcmp(target,app.chat.info.id);
    if(active) for(i=0;i<app.ntabs;i++) if(strcmp(app.tabs[i],target)) {
        next=calloc(1,sizeof(*next));
        if(!next) { note(L"Cannot delete chat: out of memory."); return 0; }
        if(!store_load(&app.store,app.tabs[i],next)) { note(app.store.error); free(next); return 0; }
        break;
    }
    if(!store_delete(&app.store,target)) {
        if(next) { chat_clear(next); free(next); }
        note(app.store.error); return 0;
    }
    for(i=0,j=0;i<app.ntabs;i++) if(strcmp(app.tabs[i],target)) {
        if(i!=j) strcpy(app.tabs[j],app.tabs[i]);
        j++;
    }
    app.ntabs=j;
    if(active) {
        chat_clear(&app.chat); app.dirty=0; KillTimer(app.window,3);
        if(next) {
            app.chat=*next; free(next);
            for(workspace=0;workspace<app.store.nworkspaces;workspace++)
                if(!strcmp(app.store.workspaces[workspace].id,app.chat.info.workspace)) break;
            app.selected=workspace;
        }
        listworkspaces(); showchat();
    } else { listchats(); synctabs(); refreshcontrols(); }
    savelayout(); note(L"Conversation deleted."); return 1;
}

static void confirmdelete(const char *id)
{
    size_t i;
    wchar_t *title, question[512];
    char target[33];
    if(app.busy && !strcmp(id,app.streamchat)) { note(L"Stop the reply before deleting this chat."); return; }
    for(i=0;i<app.store.nchats;i++) if(!strcmp(app.store.chats[i].id,id)) break;
    if(i==app.store.nchats) return;
    strcpy(target,id);
    title=wide(app.store.chats[i].title);
    swprintf(question,512,L"Delete \"%ls\"?\n\nThe saved conversation and its draft will be permanently deleted.",title?title:L"this conversation");
    free(title);
    if(MessageBoxW(app.window,question,L"Delete conversation",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES)
        deletechat(target);
}

static int sidebarchat(int row,char id[33])
{
    SidebarRow item;
    if(row<0 || row>=app.nsidebar) return 0;
    item=app.sidebar[row];
    if(item.kind!=SidebarChat || item.index<0 || (size_t)item.index>=app.store.nchats) return 0;
    strcpy(id,app.store.chats[item.index].id); return 1;
}

static void sidebaroptions(void)
{
    HMENU menu=CreatePopupMenu(); RECT r; int command;
    if(!menu) return;
    AppendMenuW(menu,MF_STRING,IdNewWorkspace,L"New workspace...");
    AppendMenuW(menu,MF_STRING,IdEditWorkspace,L"Workspace settings...");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING,IdNew,L"New chat");
    GetWindowRect(app.sidebarmenu,&r);
    command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,r.left,r.bottom,0,app.window,NULL);
    DestroyMenu(menu);
    if(command) SendMessageW(app.window,WM_COMMAND,command,0);
}

static void sidebaractivate(int row,int options)
{
    SidebarRow item;
    if(row<0 || row>=app.nsidebar) return;
    item=app.sidebar[row];
    if(item.kind==SidebarChat) {
        if(options) {
            RECT r; POINT p;
            SendMessageW(app.modules,LB_GETITEMRECT,row,(LPARAM)&r);
            p.x=r.right-px(24); p.y=r.bottom-1; ClientToScreen(app.modules,&p);
            SendMessageW(app.window,WM_CONTEXTMENU,(WPARAM)app.modules,MAKELPARAM(p.x,p.y));
        } else if(item.index>=0 && (size_t)item.index<app.store.nchats) activatechat(app.store.chats[item.index].id);
    } else if(item.kind==SidebarWorkspace && item.index>=0 && (size_t)item.index<app.store.nworkspaces) {
        wchar_t key[33];
        swprintf(key,33,L"%hs",app.store.workspaces[item.index].id);
        if(options) {
            HMENU menu=CreatePopupMenu(); POINT point; int command;
            size_t previous=app.selected;
            if(!menu) return;
            AppendMenuW(menu,MF_STRING,IdNew,L"New chat");
            AppendMenuW(menu,MF_STRING,IdEditWorkspace,L"Workspace settings...");
            GetCursorPos(&point);
            command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,app.window,NULL);
            DestroyMenu(menu);
            if(command==IdNew && savechat()) {
                if(store_new(&app.store,app.store.workspaces[item.index].id,&app.chat)) {
                    app.selected=(size_t)item.index; saveint(L"workspace-layout",key,1);
                    listworkspaces(); showchat();
                } else note(app.store.error);
            } else if(command==IdEditWorkspace) {
                app.selected=(size_t)item.index; workspaceedit(0);
                app.selected=previous; listworkspaces(); showchat();
            }
        } else {
            saveint(L"workspace-layout",key,!item.expanded); listchats();
        }
    }
}

/* Copies a title within StoreName bytes without splitting a UTF-8 sequence. */
static void settitle(char *to,const char *from)
{
    size_t n=strlen(from);
    if(n>=StoreName) { n=StoreName-1; while(n && ((unsigned char)from[n]&0xc0)==0x80) n--; }
    memcpy(to,from,n); to[n]=0;
}

/* Cuts at a word boundary within the byte limit, adding an ellipsis. */
static void clipwords(char *s,size_t limit)
{
    size_t n=strlen(s),cut;
    if(n<=limit) return;
    cut=limit;
    while(cut>limit/2 && s[cut]!=' ') cut--;
    if(cut<=limit/2) { cut=limit; while(cut && ((unsigned char)s[cut]&0xc0)==0x80) cut--; }
    while(cut && strchr(" ,;:-",s[cut-1])) cut--;
    strcpy(s+cut,"\xe2\x80\xa6");
}

/* An immediate title from the first message: drop greetings, keep the first sentence. */
static void quicktitle(const char *text,char out[StoreName])
{
    static const char *openers[]={"hey there","hey claude","hey","hi there","hi","hello","yo",
        "ok so","okay so","so","please","can you please","could you please","can you","could you",
        "would you","i need you to","i want you to","help me","quick question"};
    char buf[512]; size_t i,j=0; int trimmed=1;
    for(i=0;text[i] && j<sizeof(buf)-1;i++) {
        char c=text[i];
        if(c==' ' || c=='\t' || c=='\r' || c=='\n') { if(j && buf[j-1]!=' ') buf[j++]=' '; }
        else buf[j++]=c;
    }
    while(j && buf[j-1]==' ') j--;
    buf[j]=0;
    while(trimmed) {
        size_t k;
        trimmed=0;
        for(k=0;k<sizeof(openers)/sizeof(openers[0]);k++) {
            size_t n=strlen(openers[k]),at=n;
            if(_strnicmp(buf,openers[k],n) || (buf[n] && !strchr(" ,.!?:",buf[n]))) continue;
            while(buf[at] && strchr(" ,.!?:",buf[at])) at++;
            if(buf[at]) { memmove(buf,buf+at,strlen(buf+at)+1); trimmed=1; }
            break;
        }
    }
    {
        char *end=strpbrk(buf,".?!");
        if(end && end-buf>=12) end[*end=='?'?1:0]=0;
    }
    clipwords(buf,56);
    if(buf[0]>='a' && buf[0]<='z') buf[0]=(char)(buf[0]-32);
    settitle(out,buf[0]?buf:"New chat");
}

enum { ChangePin, ChangeMove, ChangeRename, ChangeTitle };

/* Pin, move or retitle a chat, open or not, without changing its place. */
static void changechat(const char *id,int kind,const char *value)
{
    Chat *chat;
    char target[33];
    int current=!strcmp(id,app.chat.info.id);
    size_t k;
    strcpy(target,id);
    if(current) {
        if(kind!=ChangeTitle && !savechat()) return;
        chat=&app.chat;
    } else {
        chat=calloc(1,sizeof(*chat));
        if(!chat || !store_load(&app.store,target,chat)) {
            if(chat) { chat_clear(chat); free(chat); }
            note(app.store.error); return;
        }
    }
    if(kind==ChangePin) chat->info.pinned=!chat->info.pinned;
    else if(kind==ChangeMove) strcpy(chat->info.workspace,value);
    else if(kind==ChangeRename) { settitle(chat->info.title,value); chat->info.renamed=1; }
    else if(!chat->info.renamed) settitle(chat->info.title,value);
    if(!store_save(&app.store,chat)) note(app.store.error);
    if(current && kind==ChangeMove) {
        for(k=0;k<app.store.nworkspaces;k++) if(!strcmp(app.store.workspaces[k].id,value)) app.selected=k;
        listworkspaces();
    }
    if(!current) { chat_clear(chat); free(chat); }
    listchats(); synctabs(); refreshtitle(); InvalidateRect(app.window,NULL,FALSE);
}

static void finishrename(int save)
{
    HWND w=app.renamer;
    char id[33],*title;
    wchar_t text[StoreName];
    if(!w) return;
    app.renamer=NULL; strcpy(id,app.renaming); app.renaming[0]=0;
    GetWindowTextW(w,text,StoreName); DestroyWindow(w);
    if(save) {
        title=utf8(text);
        if(title) {
            char *s=title,*e;
            while(*s==' ') s++;
            e=s+strlen(s); while(e>s && e[-1]==' ') *--e=0;
            if(*s) changechat(id,ChangeRename,s);
        }
        free(title);
    }
    SetFocus(app.modules);
}

static LRESULT CALLBACK renameproc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
    (void)data;
    if(message==WM_KEYDOWN && (wp==VK_RETURN || wp==VK_ESCAPE)) { finishrename(wp==VK_RETURN); return 0; }
    if(message==WM_CHAR && (wp==L'\r' || wp==27)) return 0;
    if(message==WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    if(message==WM_KILLFOCUS) { LRESULT result=DefSubclassProc(window,message,wp,lp); finishrename(1); return result; }
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,renameproc,id);
    return DefSubclassProc(window,message,wp,lp);
}

/* An in-place title editor over the chat's row: Enter saves, Escape cancels. */
static void beginrename(const char *id)
{
    int row;
    RECT r;
    wchar_t *title;
    size_t k;
    if(app.renamer) return;
    for(k=0;k<app.store.nchats;k++) if(!strcmp(app.store.chats[k].id,id)) break;
    if(k==app.store.nchats) return;
    for(row=0;row<app.nsidebar;row++) if(app.sidebar[row].kind==SidebarChat && (size_t)app.sidebar[row].index==k) break;
    if(row==app.nsidebar) { SetWindowTextW(app.chatsearch,L""); listchats();
        for(row=0;row<app.nsidebar;row++) if(app.sidebar[row].kind==SidebarChat && (size_t)app.sidebar[row].index==k) break; }
    if(row==app.nsidebar) return;
    SendMessageW(app.modules,LB_SETCURSEL,row,0);
    if(SendMessageW(app.modules,LB_GETITEMRECT,row,(LPARAM)&r)==LB_ERR) return;
    title=wide(app.store.chats[k].title);
    app.renamer=CreateWindowExW(0,L"EDIT",title?title:L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
        r.left+px(4),r.top+px(2),r.right-r.left-px(8),r.bottom-r.top-px(4),app.modules,NULL,GetModuleHandleW(NULL),NULL);
    free(title);
    if(!app.renamer) return;
    strcpy(app.renaming,app.store.chats[k].id);
    SendMessageW(app.renamer,WM_SETFONT,(WPARAM)app.normal,TRUE);
    SendMessageW(app.renamer,EM_SETLIMITTEXT,StoreName/2,0);
    SetWindowSubclass(app.renamer,renameproc,1,0);
    SetFocus(app.renamer); SendMessageW(app.renamer,EM_SETSEL,0,-1);
}

typedef struct TitleWork { char id[33]; char *body; unsigned short port; HANDLE cancel; char title[StoreName]; } TitleWork;

static DWORD WINAPI titleworker(void *arg)
{
    TitleWork *t=arg;
    char *answer=NULL,error[256];
    int rc=LocalJsonFailed,attempt;
    /* The engine may still be closing the reply stream; give it a moment. */
    for(attempt=0;attempt<3 && rc!=LocalJsonOk;attempt++) {
        if(WaitForSingleObject(t->cancel,attempt?1000:300)==WAIT_OBJECT_0) break;
        free(answer); answer=NULL;
        rc=local_json(t->port,L"/v1/chat/completions",t->body,t->cancel,&answer,error,sizeof(error));
    }
    if(rc==LocalJsonOk && answer) {
        cJSON *json=cJSON_Parse(answer);
        cJSON *first=json?cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json,"choices"),0):NULL;
        cJSON *message=first?cJSON_GetObjectItemCaseSensitive(first,"message"):NULL;
        cJSON *content=message?cJSON_GetObjectItemCaseSensitive(message,"content"):NULL;
        if(cJSON_IsString(content)) {
            /* Models add quotes, labels or trailing punctuation. */
            char buf[StoreName],*s=content->valuestring,*e;
            while(*s && strchr(" \t\r\n",*s)) s++;
            if(!_strnicmp(s,"title:",6)) s+=6;
            settitle(buf,s);
            if((e=strpbrk(buf,"\r\n"))) *e=0;
            s=buf; while(*s && strchr(" \t\"'*#`.",*s)) s++;
            e=s+strlen(s); while(e>s && strchr(" \t\"'*#`.",e[-1])) *--e=0;
            clipwords(s,60);
            settitle(t->title,s);
        }
        cJSON_Delete(json);
    }
    free(answer);
    PostMessageW(app.window,TitleReady,0,(LPARAM)t);
    return 0;
}

/* Asks the loaded model for a short title; the instant one stays otherwise. */
static void suggesttitle(const char *id,const char *question,const char *answer,unsigned short port)
{
    TitleWork *t=calloc(1,sizeof(*t));
    cJSON *body=cJSON_CreateObject(),*messages=cJSON_AddArrayToObject(body,"messages"),*m,*kwargs;
    char user[1200];
    HANDLE thread;
    if(!t || !body || !messages) { free(t); cJSON_Delete(body); return; }
    snprintf(user,sizeof(user),"User: %.600s\nAssistant: %.300s\n\nTitle:",question,answer);
    while(strlen(user) && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,user,-1,NULL,0)) user[strlen(user)-1]=0;
    m=cJSON_CreateObject(); cJSON_AddStringToObject(m,"role","system");
    cJSON_AddStringToObject(m,"content","You name chat conversations. Reply with only a title of 2 to 6 words "
        "in the language of the conversation. No quotes. No final punctuation.");
    cJSON_AddItemToArray(messages,m);
    m=cJSON_CreateObject(); cJSON_AddStringToObject(m,"role","user"); cJSON_AddStringToObject(m,"content",user);
    cJSON_AddItemToArray(messages,m);
    cJSON_AddStringToObject(body,"model","local");
    cJSON_AddNumberToObject(body,"max_tokens",24); cJSON_AddNumberToObject(body,"temperature",0.2);
    kwargs=cJSON_AddObjectToObject(body,"chat_template_kwargs"); cJSON_AddBoolToObject(kwargs,"enable_thinking",0);
    cJSON_AddBoolToObject(body,"stream",0);
    t->body=cJSON_PrintUnformatted(body); cJSON_Delete(body);
    strcpy(t->id,id); t->port=port; t->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!t->body || !t->cancel) { if(t->cancel) CloseHandle(t->cancel); cJSON_free(t->body); free(t); return; }
    if(app.titlecancel) SetEvent(app.titlecancel);
    app.titlecancel=t->cancel;
    thread=CreateThread(NULL,0,titleworker,t,0,NULL);
    if(thread) CloseHandle(thread);
    else { app.titlecancel=NULL; CloseHandle(t->cancel); cJSON_free(t->body); free(t); }
}

/* Inserts a UTF-8 text or code file into the message as a tagged block. */
static void inserttext(void)
{
    OPENFILENAMEW of={0};
    wchar_t path[MAX_PATH]={0},*text,*block;
    const wchar_t *name;
    HANDLE file;
    DWORD size,got=0;
    char *data,*start;
    enum { Limit=12*1024 };
    of.lStructSize=sizeof(of); of.hwndOwner=app.window; of.lpstrFile=path; of.nMaxFile=MAX_PATH;
    of.lpstrTitle=L"Insert a text or code file";
    of.lpstrFilter=L"Text and code\0*.txt;*.md;*.csv;*.tsv;*.json;*.jsonl;*.xml;*.yaml;*.yml;*.toml;*.ini;*.cfg;*.log;"
        L"*.html;*.htm;*.css;*.js;*.ts;*.tsx;*.jsx;*.py;*.c;*.h;*.cpp;*.hpp;*.cc;*.cs;*.java;*.kt;*.go;*.rs;*.rb;"
        L"*.php;*.sh;*.ps1;*.bat;*.sql;*.lua;*.swift;*.r;*.tex;*.srt;*.vtt\0All files\0*.*\0\0";
    of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
    if(!GetOpenFileNameW(&of)) return;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) { note(L"Cannot open the file."); return; }
    size=GetFileSize(file,NULL);
    if(size==INVALID_FILE_SIZE || size>Limit) { CloseHandle(file); note(L"Text files are limited to 12 KiB on Windows."); return; }
    data=calloc(size+1,1);
    if(!data || !ReadFile(file,data,size,&got,NULL) || got!=size) { CloseHandle(file); free(data); note(L"Cannot read the file."); return; }
    CloseHandle(file);
    start=data; if(!strncmp(start,"\xef\xbb\xbf",3)) start+=3;
    text=wide(start); free(data);
    if(!text) { note(L"This file is not UTF-8 text."); return; }
    name=wcsrchr(path,L'\\'); name=name?name+1:path;
    block=malloc((wcslen(text)+wcslen(name)+64)*sizeof(*block));
    if(block) {
        int length=GetWindowTextLengthW(app.prompt);
        swprintf(block,wcslen(text)+wcslen(name)+64,L"%ls<file name=\"%ls\">\r\n%ls\r\n</file>\r\n",length?L"\r\n\r\n":L"",name,text);
        SendMessageW(app.prompt,EM_SETSEL,(WPARAM)length,(LPARAM)length);
        SendMessageW(app.prompt,EM_REPLACESEL,TRUE,(LPARAM)block);
        SetFocus(app.prompt);
    }
    free(block); free(text);
}

static void chatmenu(LPARAM position)
{
    POINT point={GET_X_LPARAM(position),GET_Y_LPARAM(position)};
    int item;
    HMENU menu;
    char id[33];
    int command, i;

    if(point.x==-1 && point.y==-1) {
        RECT r;
        item=(int)SendMessageW(app.modules,LB_GETCURSEL,0,0);
        if(item<0 || SendMessageW(app.modules,LB_GETITEMRECT,item,(LPARAM)&r)==LB_ERR) return;
        point.x=r.left; point.y=r.bottom; ClientToScreen(app.modules,&point);
    } else {
        LRESULT hit; POINT local=point; RECT r;
        ScreenToClient(app.modules,&local);
        hit=SendMessageW(app.modules,LB_ITEMFROMPOINT,0,MAKELPARAM(local.x,local.y));
        item=LOWORD(hit);
        if(HIWORD(hit) || item>=app.nsidebar || SendMessageW(app.modules,LB_GETITEMRECT,item,(LPARAM)&r)==LB_ERR || !PtInRect(&r,local)) return;
    }
    if(!sidebarchat(item,id)) { sidebaractivate(item,1); return; }
    for(i=0;i<app.ntabs;i++) if(!strcmp(app.tabs[i],id)) break;
    menu=CreatePopupMenu(); if(!menu) return;
    {
        size_t k,index=app.store.nchats;
        HMENU move=CreatePopupMenu();
        for(k=0;k<app.store.nchats;k++) if(!strcmp(app.store.chats[k].id,id)) index=k;
        AppendMenuW(menu,MF_STRING,IdOpenChat,L"&Open");
        AppendMenuW(menu,MF_STRING,IdRename,L"&Rename\tF2");
        AppendMenuW(menu,MF_STRING,IdPin,index<app.store.nchats && app.store.chats[index].pinned?L"Un&pin":L"&Pin to top");
        if(move) {
            int added=0;
            for(k=0;k<app.store.nworkspaces;k++) {
                wchar_t *name;
                if(index<app.store.nchats && !strcmp(app.store.workspaces[k].id,app.store.chats[index].workspace)) continue;
                name=wide(app.store.workspaces[k].name);
                AppendMenuW(move,MF_STRING,IdMoveFirst+(UINT)k,name?name:L"Workspace"); free(name); added++;
            }
            if(added) AppendMenuW(menu,MF_POPUP,(UINT_PTR)move,L"&Move to");
            else DestroyMenu(move);
        }
    }
    AppendMenuW(menu,MF_STRING|(i<app.ntabs?0:MF_GRAYED),IdCloseTab,L"&Close tab");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING,IdDeleteChat,L"&Delete conversation...\tDel");
    SendMessageW(app.modules,LB_SETCURSEL,item,0);
    command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,app.window,NULL);
    DestroyMenu(menu);
    if(command==IdOpenChat) activatechat(id);
    else if(command==IdRename) beginrename(id);
    else if(command==IdPin) changechat(id,ChangePin,NULL);
    else if(command>=IdMoveFirst && (size_t)(command-IdMoveFirst)<app.store.nworkspaces)
        changechat(id,ChangeMove,app.store.workspaces[command-IdMoveFirst].id);

    else if(command==IdDeleteChat) confirmdelete(id);
    else if(command==IdCloseTab && i<app.ntabs) {
        if(!strcmp(id,app.chat.info.id)) closetab();
        else {
            memmove(app.tabs+i,app.tabs+i+1,(size_t)(app.ntabs-i-1)*sizeof(app.tabs[0]));
            app.ntabs--; synctabs();
        }
        savelayout();
    }
}
static DWORD WINAPI readsettings(void *context)
{
    SettingsWork *work=context;
    model_settings_load(work->config,work->model,&work->settings);
    work->model_ok=model_read(work->model,&work->info);
    if(!PostMessageW(work->window,SettingsReady,0,(LPARAM)work)) free(work);
    return 0;
}
static void loadsettings(void)
{
    SettingsWork *work=calloc(1,sizeof(*work));
    HANDLE thread;
    app.settings_epoch++;
    app.settings_pending=1;
    app.model_ok=0; memset(&app.model_info,0,sizeof(app.model_info));
    memset(&app.budget,0,sizeof(app.budget));
    if(work) {
        work->window=app.window; work->epoch=app.settings_epoch;
        wcscpy(work->model,app.modelpath); wcscpy(work->config,app.config);
        thread=CreateThread(NULL,0,readsettings,work,0,NULL);
        if(thread) { CloseHandle(thread); refreshcontrols(); return; }
        free(work);
    }
    note(L"Cannot read model settings. Select the model again.");
    refreshcontrols();
}
static void generationrows(void)
{
    const wchar_t *names[]={L"Response tokens",L"Temperature",L"Top P",L"Context (reload)",
        L"Thinking",L"Repeat penalty",L"DRY strength",L"Top K",L"Min P",L"Presence penalty"};
    wchar_t value[64];
    LVITEMW item={0};
    int i;
    ListView_DeleteAllItems(app.generation);
    for(i=0;i<GenCount;i++) {
        item.mask=LVIF_TEXT; item.iItem=i; item.pszText=(wchar_t *)names[i];
        ListView_InsertItem(app.generation,&item);
        switch(i) {
        case 0: swprintf(value,64,L"%d",app.settings.max_tokens); break;
        case 1: swprintf(value,64,L"%.2f",app.settings.temperature); break;
        case 2: swprintf(value,64,L"%.2f",app.settings.top_p); break;
        case 3: swprintf(value,64,L"%d",app.settings.context_tokens); break;
        case 4: wcscpy(value,app.settings.thinking==ThinkingAuto?L"Auto":app.settings.thinking==ThinkingOff?L"Off":L"On"); break;
        case 5: swprintf(value,64,L"%.2f",app.settings.repeat_penalty); break;
        case 6: swprintf(value,64,L"%.2f",app.settings.dry_multiplier); break;
        case 7: swprintf(value,64,L"%d",app.settings.top_k); break;
        case 8: swprintf(value,64,L"%.2f",app.settings.min_p); break;
        default: swprintf(value,64,L"%.2f",app.settings.presence_penalty); break;
        }
        ListView_SetItemText(app.generation,i,1,value);
    }
    SetWindowTextW(app.genhelp,L"Double-click a value or press Enter. Enter saves; Escape cancels.\r\n\r\nSaved per model. Reload after changing context.\r\n\r\nThinking: Auto, Off, or On. Auto uses the template default.\r\n\r\nRepeat: 1 = off. DRY, Top K, Min P, Presence: 0 = off.");
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
    } else if(end!=value && !*end) {
        valid=generation_set(&next,row,number);
    }
    ShowWindow(app.value,SW_HIDE);
    if(!valid || !generation_valid(&next))
        note(L"Invalid setting. Response: 1-16384; context: 512-1048576; temperature: 0-2; Top P: 0.01-1; repeat: 1-2; DRY: 0-4; Top K: 0-1000; Min P: 0-1; presence: -2 to 2.");
    else if(!model_settings_save_field(app.config,app.modelpath,&next,row))
        note(L"Cannot save model settings. Select a local model and check the settings folder. Previous values retained.");
    else {
        app.settings=next;
        if(row==GenThinking) loadsettings();
        note(row==3?L"Context saved for this model. Unload and load again to apply it.":L"Settings saved for this model. They apply to the next message.");
    }
    generationrows();
}
static void editproperty(void)
{
    RECT r;
    wchar_t value[64];
    int row=ListView_GetNextItem(app.generation,-1,LVNI_SELECTED);
    if(app.busy || app.settings_pending || row<0 || row>=GenCount) return;
    commitproperty();
    if(app.settings_pending) return;
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
    case SettingsReady: {
        SettingsWork *work=(SettingsWork *)lp;
        if(work->epoch==app.settings_epoch && !_wcsicmp(work->model,app.modelpath)) {
            app.settings=work->settings; app.settings_pending=0;
            app.model_ok=work->model_ok; app.model_info=work->info;
            generationrows(); refreshcontrols();
        }
        free(work); return 0;
    }
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
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: return 0;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX: {
		HDC dc=(HDC)wp;
		int ispaper=(HWND)lp==app.prompt || (HWND)lp==app.model || (HWND)lp==app.port ||
		    (HWND)lp==app.activity || (HWND)lp==app.chatsearch || (HWND)lp==app.folder || (HWND)lp==app.master;
		SetTextColor(dc,(HWND)lp==app.status?Muted:Ink);
        if((HWND)lp==app.modules) { SetBkColor(dc,Light); SetDCBrushColor(dc,Light); return (LRESULT)GetStockObject(DC_BRUSH); }
        SetBkColor(dc,ispaper?Paper:Face);
        return (LRESULT)(ispaper?app.paper:app.face);
	}
	case WM_DRAWITEM: {
		DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT *)lp;
		if(d->CtlType==ODT_BUTTON) { drawbutton(d); return TRUE; }
		if(d->CtlType==ODT_TAB) { drawtab(d); return TRUE; }
        if(d->CtlType==ODT_LISTBOX && d->CtlID==IdModules) { drawsidebar(d); return TRUE; }
		break;
	}
	case WM_MEASUREITEM:
		((MEASUREITEMSTRUCT *)lp)->itemHeight=(UINT)px(((MEASUREITEMSTRUCT *)lp)->CtlID==IdModules?28:22); return TRUE;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lp)->ptMinTrackSize.x=px(1000);
		((MINMAXINFO *)lp)->ptMinTrackSize.y=px(680);
		return 0;
	case WM_TIMER:
		if(wp==1) pollengine();
		else if(wp==3) { KillTimer(window,3); savechat(); }
		else if(wp==2 && (app.busy || app.health==EngineLoading)) {
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
			if(app.health==EngineReady && app.pendingsend) { app.pendingsend=0; submit(); }
			else if(app.health==EngineOffline && !app.process.process) app.pendingsend=0;
		}
		free(p); return 0;
	}
    case TitleReady: {
        TitleWork *t=(TitleWork *)lp;
        if(t->title[0] && WaitForSingleObject(t->cancel,0)!=WAIT_OBJECT_0) changechat(t->id,ChangeTitle,t->title);
        if(app.titlecancel==t->cancel) app.titlecancel=NULL;
        CloseHandle(t->cancel); cJSON_free(t->body); free(t);
        return 0;
    }
	case LibraryReady: modellist((ScanJob *)lp); return 0;
	case WM_NOTIFY: {
		NMHDR *n=(NMHDR *)lp;
        if(n->code==TTN_GETDISPINFOW && ((NMTTDISPINFOW *)lp)->hdr.idFrom==(UINT_PTR)app.modules) {
            NMTTDISPINFOW *tip=(NMTTDISPINFOW *)lp;
            if(app.sidebarhover>=0 && app.sidebarhover<app.nsidebar) {
                SendMessageW(app.modules,LB_GETTEXT,app.sidebarhover,(LPARAM)app.sidebartip);
                tip->lpszText=app.sidebartip;
            }
            return 0;
        }
        if(n->idFrom==IdGeneration && (n->code==NM_DBLCLK || n->code==NM_RETURN)) editproperty();
        else if(n->idFrom==IdChatTabs && n->code==TCN_SELCHANGE) {
            int index=TabCtrl_GetCurSel(app.chattabs);
            if(index>=0 && index<app.ntabs) activatechat(app.tabs[index]);
        } else if(n->idFrom==IdOutputTabs && n->code==TCN_SELCHANGE) {
            layout();
        } else if(n->idFrom==IdLibraryTabs && n->code==TCN_SELCHANGE) {
            commitproperty(); layout(); SetFocus(n->hwndFrom);
        } else if(n->idFrom==IdModels) {
			if(n->code==LVN_ITEMCHANGED) {
				NMLISTVIEW *change=(NMLISTVIEW *)lp;
				if((change->uChanged&LVIF_STATE) && ((change->uOldState^change->uNewState)&LVIS_SELECTED)) selectmodel(0);
			}
			else if(n->code==LVN_COLUMNCLICK) {
				int column=((NMLISTVIEW *)lp)->iSubItem;
				if(column>=0 && column<=1) {
					app.sortdescending=column==app.sortcolumn?!app.sortdescending:0;
					app.sortcolumn=column; sortmodels();
				}
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
	case WM_CONTEXTMENU:
		if((HWND)wp==app.modules) { chatmenu(lp); return 0; }
		break;
	case WM_COMMAND:
		switch(LOWORD(wp)) {
        case IdEngineArguments: {
            wchar_t extra[2048];
            if(app.busy || app.process.process) { note(L"Unload the model before changing engine arguments."); break; }
            wcscpy(extra,app.extra_arguments);
            if(edit_engine_arguments(app.window,app.normal,extra,2048)) {
                if(!engine_arguments_valid(extra)) note(L"Extra arguments contain unmatched quotes or control characters.");
                else if(!WritePrivateProfileStringW(L"engine",L"extra_arguments",extra,app.config)) note(L"Cannot save engine arguments.");
                else { wcscpy(app.extra_arguments,extra); note(L"Engine arguments saved. Load the model to apply them."); }
            }
            break;
        }
        case IdSetup: runsetup(); break;
        case IdStop: stopreply(); break;
        case IdCloseTab: closetab(); break;
        case IdDeleteChat: if(app.ntabs) confirmdelete(app.chat.info.id); break;
        case IdValue: if(HIWORD(wp)==EN_KILLFOCUS) commitproperty(); break;
        case IdViewLeft: commitproperty(); app.hideleft=!app.hideleft; layout(); savelayout(); break;
        case IdViewChats: app.hidechats=!app.hidechats; layout(); savelayout(); break;
        case IdLatest: latest(); break;
        case IdViewRight: commitproperty(); app.hideright=!app.hideright; layout(); savelayout(); break;
        case IdViewOutput: app.hideoutput=!app.hideoutput; layout(); savelayout(); break;
        case IdResetLayout:
            commitproperty(); app.hideleft=app.hideright=app.hideoutput=0;
            app.hidechats=0; app.leftsize=244; app.rightsize=280; app.outputsize=128; layout(); savelayout(); break;
        case IdGeneration:
            app.hideright=0; TabCtrl_SetCurSel(app.librarytabs,1); layout(); SetFocus(app.generation); break;
        case IdSend: submit(); break;
		case IdRetry: repeat_exchange(0); break;
		case IdResend: repeat_exchange(1); break;
		case IdExchange:
			if(HIWORD(wp)==CBN_SELCHANGE && !app.busy) {
				LRESULT row=SendMessageW(app.exchange,CB_GETCURSEL,0,0);
				if(row>=0) {
					CHARRANGE caret;
					app.turn=(size_t)row; followchat(0);
					caret.cpMin=caret.cpMax=(LONG)SendMessageW(app.exchange,CB_GETITEMDATA,(WPARAM)row,0);
					SendMessageW(app.transcript,EM_EXSETSEL,0,(LPARAM)&caret);
					SendMessageW(app.transcript,EM_SCROLLCARET,0,0);
				}
			}
			break;
		case IdLoad: loadmodel(); break;
		case IdUnload:
			if(!app.busy && app.process.process) { app.epoch++; engine_stop(&app.process); app.health=EngineOffline; note(L"Model unloaded."); refreshcontrols(); }
			break;
		case IdBrowse: if(!app.busy && !app.process.process) choosefile(0); break;
		case IdEngine: if(!app.busy && !app.process.process) choosefile(1); break;
		case IdCopy: copyselection(); break;
		case IdNew:
			if(savechat()) {
				if(app.chat.info.id[0] && !app.chat.conversation.count && !app.chat.draft[0]) showchat();
				else if(store_new(&app.store,app.store.workspaces[app.selected].id,&app.chat)) { showchat(); note(L"New chat saved."); }
				else note(app.store.error);
			}
			break;

        case IdChatSearch:
            if(HIWORD(wp)==EN_CHANGE && app.modules) { SendMessageW(app.modules,LB_SETTOPINDEX,0,0); listchats(); }
            break;
        case IdSidebarMenu: sidebaroptions(); break;
        case IdInsertText: inserttext(); break;
        case IdRename: if(app.chat.info.id[0]) beginrename(app.chat.info.id); break;
        case IdPin: if(app.chat.info.id[0]) changechat(app.chat.info.id,ChangePin,NULL); break;
        case IdFindChat: SetFocus(app.chatsearch); SendMessageW(app.chatsearch,EM_SETSEL,0,-1); break;
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
		case IdShortcuts: show_shortcuts(window,app.normal); break;
		case IdAbout:
			MessageBoxW(window,L"lcb-ai\nVersion 0.18.0\n\nLocal AI processor and interface.\n\n"
			    L"Larkin Computing Bureau",L"About lcb-ai",MB_OK);
			break;
		}
		return 0;
    case TelemetryReady: {
        StreamSnapshot *snapshot=(StreamSnapshot *)lp;
        if(app.busy && app.work==snapshot->source) {
            free(app.live); app.live=snapshot;
            if((snapshot->content || snapshot->trace[0]) && !app.firstseconds)
                app.firstseconds=(double)(GetTickCount64()-app.started)/1000.0;
            if(snapshot->content) wcscpy(app.phase,L"Replying");
            else if(snapshot->trace[0]) wcscpy(app.phase,L"Thinking");
            else if(snapshot->total>0) swprintf(app.phase,160,L"Reading prompt: %d / %d (%d cached)",
                snapshot->processed,snapshot->total,snapshot->cached);
            else wcscpy(app.phase,L"Waiting for model");
            SetWindowTextW(app.progress,app.phase);
            if(snapshot->timings.tokens_per_second>0) {
                wchar_t speed[64]; swprintf(speed,64,L"%.1f tok/s",snapshot->timings.tokens_per_second);
                SetWindowTextW(app.speed,speed);
            }
            if(streaminghere()) render_stream();
            InvalidateRect(app.window,NULL,FALSE);
            return 0;
        }
        free(snapshot); return 0;
    }
    case BudgetReady: {
        Work *w=(Work *)lp;
        wchar_t summary[320];
        app.budget=w->budget;
        swprintf(summary,320,L"Context: %d + %d reply / %d | %zu older excluded",
            w->budget.prompt_tokens,w->budget.response_tokens,w->budget.context_tokens,w->budget.omitted_messages/2);
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
	case WM_DESTROY:
        free(app.live); app.live=NULL;
        free(app.sections); app.sections=NULL;
        free(app.thinking_closed); app.thinking_closed=NULL; app.thinking_capacity=0;
        KillTimer(window,1); KillTimer(window,2); PostQuitMessage(0); return 0;
	}
	return DefWindowProcW(window,message,wp,lp);
}

/* Runs a menu command from its keyboard shortcut unless the menu shows it
 * unavailable; the keys are listed in shortcuts.h. */
static void
shortcut(int id)
{
    UINT state=GetMenuState(GetMenu(app.window),(UINT)id,MF_BYCOMMAND);
    if(state!=(UINT)-1 && (state&(MF_GRAYED|MF_DISABLED))) return;
    SendMessageW(app.window,WM_COMMAND,(WPARAM)id,0);
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
	AppendMenuW(file,MF_STRING,IdCloseTab,L"&Close conversation tab\tCtrl+W");
	AppendMenuW(file,MF_STRING,IdDeleteChat,L"&Delete conversation...");
	AppendMenuW(file,MF_SEPARATOR,0,NULL);
	AppendMenuW(file,MF_STRING,IdNewWorkspace,L"New &workspace...");
	AppendMenuW(file,MF_STRING,IdEditWorkspace,L"Workspace &settings...");
	AppendMenuW(file,MF_SEPARATOR,0,NULL); AppendMenuW(file,MF_STRING,IdExit,L"E&xit\tCtrl+Q");
    AppendMenuW(view,MF_STRING,IdViewLeft,L"Show / hide &Chats sidebar\tF9");
    AppendMenuW(view,MF_STRING,IdViewChats,L"Show / hide chat &tabs");
    AppendMenuW(view,MF_STRING,IdViewRight,L"Show / hide &Properties");
    AppendMenuW(view,MF_STRING,IdViewOutput,L"Show / hide &Output\tF12");
    AppendMenuW(view,MF_SEPARATOR,0,NULL);
    AppendMenuW(view,MF_STRING,IdResetLayout,L"&Reset layout");
    AppendMenuW(engine,MF_STRING,IdStop,L"&Stop generation\tEsc");
    AppendMenuW(settings,MF_STRING,IdGeneration,L"&Generation properties\tAlt+Enter");
    AppendMenuW(settings,MF_STRING,IdSetup,L"&Setup...");
    AppendMenuW(settings,MF_STRING,IdEngineArguments,L"Engine &arguments...\tCtrl+,");
    AppendMenuW(edit,MF_STRING,IdCopy,L"&Copy\tCtrl+C");
	AppendMenuW(edit,MF_SEPARATOR,0,NULL);
	AppendMenuW(edit,MF_STRING,IdRetry,L"&Retry selected exchange\tCtrl+R");
	AppendMenuW(edit,MF_STRING,IdResend,L"&Edit and resend...\tCtrl+E");
	AppendMenuW(edit,MF_SEPARATOR,0,NULL);
	AppendMenuW(edit,MF_STRING,IdInsertText,L"Insert text &file...");
	AppendMenuW(edit,MF_STRING,IdFindChat,L"&Find chat\tCtrl+F");
	AppendMenuW(file,MF_STRING,IdRename,L"&Rename conversation\tF2");
	AppendMenuW(file,MF_STRING,IdPin,L"&Pin or unpin conversation");
	AppendMenuW(engine,MF_STRING,IdBrowse,L"Select &model...\tCtrl+O");
	AppendMenuW(engine,MF_STRING,IdEngine,L"Select &engine...");
	AppendMenuW(engine,MF_SEPARATOR,0,NULL);
	AppendMenuW(engine,MF_STRING,IdLoad,L"&Load model\tF5");
	AppendMenuW(engine,MF_STRING,IdUnload,L"&Unload model");
	AppendMenuW(settings,MF_STRING,IdLibrary,L"Model &library...");
	AppendMenuW(settings,MF_STRING,IdEditWorkspace,L"&Workspace...");
	AppendMenuW(help,MF_STRING,IdShortcuts,L"&Keyboard shortcuts\tF1");
	AppendMenuW(help,MF_SEPARATOR,0,NULL);
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
	app.fresh=control(L"BUTTON",L"+",WS_TABSTOP|BS_OWNERDRAW,IdNew);
	app.load=control(L"BUTTON",L"&Load",WS_TABSTOP|BS_OWNERDRAW,IdLoad);
	app.unload=control(L"BUTTON",L"&Unload",WS_TABSTOP|BS_OWNERDRAW,IdUnload);
	app.engine=control(L"BUTTON",L"&Engine...",WS_TABSTOP|BS_OWNERDRAW,IdEngine);
	app.status=control(L"STATIC",L"Ready",SS_LEFT,0);
	app.workspace=control(L"COMBOBOX",L"Workspace",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,IdWorkspace);
	app.newworkspace=control(L"BUTTON",L"New...",WS_TABSTOP|BS_OWNERDRAW,IdNewWorkspace);
	app.editworkspace=control(L"BUTTON",L"Edit...",WS_TABSTOP|BS_OWNERDRAW,IdEditWorkspace);
	app.chatsearch=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,IdChatSearch);
    SendMessageW(app.chatsearch,EM_SETCUEBANNER,FALSE,(LPARAM)L"Search chats");
    SendMessageW(app.chatsearch,EM_SETLIMITTEXT,StoreName-1,0);
    SendMessageW(app.chatsearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(px(7),px(7)));
    app.sidebarmenu=control(L"BUTTON",L"...",WS_TABSTOP|BS_OWNERDRAW,IdSidebarMenu);
    app.modules=control(L"LISTBOX",L"Chats",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,IdModules);
    SendMessageW(app.modules,LB_SETITEMHEIGHT,0,px(28));
    app.sidebarhover=-1;
	app.transcript=control(MSFTEDIT_CLASS,L"",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0);
	SendMessageW(app.transcript,EM_SETBKGNDCOLOR,0,Paper);
	SendMessageW(app.transcript,EM_EXLIMITTEXT,0,0x7ffffffe);
	SendMessageW(app.transcript,EM_AUTOURLDETECT,0,0);
	app.exchange=control(L"COMBOBOX",L"Exchange",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,IdExchange);
	SendMessageW(app.exchange,CB_SETDROPPEDWIDTH,px(440),0);
	app.retry=control(L"BUTTON",L"&Retry",WS_TABSTOP|BS_OWNERDRAW,IdRetry);
	app.resend=control(L"BUTTON",L"Edit and resend...",WS_TABSTOP|BS_OWNERDRAW,IdResend);
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
    app.outputtabs=control(WC_TABCONTROLW,L"Output views",WS_TABSTOP,IdOutputTabs);
    tab.mask=TCIF_TEXT; tab.pszText=L"Session activity"; TabCtrl_InsertItem(app.outputtabs,0,&tab);
    app.latest=control(L"BUTTON",L"Latest",WS_TABSTOP|BS_OWNERDRAW,IdLatest);
    app.explorertoggle=control(L"BUTTON",L"<",WS_TABSTOP|BS_OWNERDRAW,IdViewLeft);
    app.chatstoggle=control(L"BUTTON",L"-",WS_TABSTOP|BS_OWNERDRAW,IdViewChats);
    app.speed=control(L"STATIC",L"",SS_RIGHT,0);
    app.progress=control(L"STATIC",L"Ready",SS_LEFT,0);
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
	tooltips(); layout(); listworkspaces(); showchat(); loadsettings(); refreshcontrols(); note(L"Ready");
	if(app.store.skipped) note(L"Some saved files could not be loaded. They were left unchanged in the application data folder.");
	SetTimer(app.window,1,2000,NULL); SetTimer(app.window,2,125,NULL);
	ShowWindow(app.window,show); UpdateWindow(app.window); SetFocus(app.prompt);
	QueryPerformanceCounter(&finish);
	swprintf(timing,128,L"Interface ready in %.1f ms.",1000.0*(double)(finish.QuadPart-start.QuadPart)/(double)frequency.QuadPart);
	note(timing);
	if(app.libraryfolder[0]) scanlibrary();
	pollengine();
	if(!setup_paths_ready(app.enginepath,app.modelpath)) PostMessageW(app.window,WM_COMMAND,IdSetup,0);
	while(GetMessageW(&msg,NULL,0,0)>0) {
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_F2 && !app.renamer) {
            char id[33];
            if(GetFocus()==app.modules && sidebarchat((int)SendMessageW(app.modules,LB_GETCURSEL,0,0),id)) beginrename(id);
            else if(app.chat.info.id[0]) beginrename(app.chat.info.id);
            continue;
        }
        /* Search: Enter opens the first result, Down moves into the list. */
        if(msg.message==WM_KEYDOWN && GetFocus()==app.chatsearch && (msg.wParam==VK_RETURN || msg.wParam==VK_DOWN)) {
            int row; char id[33];
            for(row=0;row<app.nsidebar;row++) if(app.sidebar[row].kind==SidebarChat) break;
            if(row<app.nsidebar) {
                SendMessageW(app.modules,LB_SETCURSEL,row,0);
                if(msg.wParam==VK_RETURN && sidebarchat(row,id)) { SetWindowTextW(app.chatsearch,L""); activatechat(id); SetFocus(app.prompt); }
                else SetFocus(app.modules);
            }
            continue;
        }
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_DELETE && GetFocus()==app.modules) {
            char id[33];
            if(sidebarchat((int)SendMessageW(app.modules,LB_GETCURSEL,0,0),id)) confirmdelete(id);
            continue;
        }
        if(msg.message==WM_KEYDOWN && GetFocus()==app.value) {
            if(msg.wParam==VK_RETURN) { commitproperty(); SetFocus(app.generation); continue; }
            if(msg.wParam==VK_ESCAPE) { app.property=-1; ShowWindow(app.value,SW_HIDE); SetFocus(app.generation); continue; }
        }
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && app.busy) { stopreply(); continue; }
        if(msg.message==WM_KEYDOWN && !(GetKeyState(VK_CONTROL)&0x8000)) {
            int id=msg.wParam==VK_F1?IdShortcuts:msg.wParam==VK_F5?IdLoad:msg.wParam==VK_F9?IdViewLeft:msg.wParam==VK_F12?IdViewOutput:0;
            if(id) { shortcut(id); continue; }
        }
        if(msg.message==WM_SYSKEYDOWN && msg.wParam==VK_RETURN) { shortcut(IdGeneration); continue; }
        if(msg.message==WM_KEYDOWN && GetFocus()==app.generation && msg.wParam==VK_RETURN) { editproperty(); continue; }
        if(msg.message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)) {
            if(msg.wParam=='A') {
                wchar_t kind[32]; HWND focus=GetFocus();
                GetClassNameW(focus,kind,32);
                if(!wcscmp(kind,L"Edit")) { SendMessageW(focus,EM_SETSEL,0,-1); continue; }
            }
            if(msg.wParam=='W') { closetab(); continue; }
            if(msg.wParam=='K') { SendMessageW(app.window,WM_COMMAND,IdFindChat,0); continue; }
            if((msg.wParam==VK_TAB || msg.wParam==VK_PRIOR || msg.wParam==VK_NEXT) && app.ntabs) {
                int back=msg.wParam==VK_TAB?(GetKeyState(VK_SHIFT)&0x8000)!=0:msg.wParam==VK_PRIOR;
                int index=TabCtrl_GetCurSel(app.chattabs),step=back?app.ntabs-1:1;
                activatechat(app.tabs[(index+step)%app.ntabs]); continue;
            }
			if(msg.wParam=='C' && GetFocus()==app.details) { copyselection(); continue; }
			if(msg.wParam==VK_RETURN) { submit(); continue; }
			if(msg.wParam=='N') { SendMessageW(app.window,WM_COMMAND,IdNew,0); continue; }
			{
				int id=msg.wParam=='F'?IdFindChat:msg.wParam=='Q'?IdExit:msg.wParam=='R'?IdRetry:msg.wParam=='E'?IdResend:
				    msg.wParam=='O'?IdBrowse:msg.wParam==VK_OEM_COMMA?IdEngineArguments:0;
				if(id) { shortcut(id); continue; }
			}
		}
		if(!IsDialogMessageW(app.window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	}
	engine_stop(&app.process);
	chat_clear(&app.chat); store_close(&app.store);
	free(app.library); free(app.sidebar);
	if(app.icons) ImageList_Destroy(app.icons);
	DeleteObject(app.normal); DeleteObject(app.fixed); DeleteObject(app.heading);
	DeleteObject(app.face); DeleteObject(app.paper); FreeLibrary(app.rich);
	return 0;
}

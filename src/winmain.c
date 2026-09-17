#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include "lti.h"

enum { IdModules = 100, IdPrompt, IdSend, IdNew, IdPort, IdExit, IdAbout,
	ReplyReady = WM_APP + 1 };

typedef struct Work Work;
struct Work {
	HWND target;
	char *request;
	char *prompt;
	char *answer;
	char error[256];
	unsigned short port;
	int ok;
};

static struct {
	HWND window, title, subtitle, modules, transcript, prompt, send, fresh;
	HWND status, port, portlabel, promptlabel, modulelabel;
	HFONT normal, fixed, heading;
	Conversation conversations[3];
	size_t selected;
	int busy, dpi;
} app;

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
append(const wchar_t *s)
{
	SendMessage(app.transcript, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	SendMessage(app.transcript, EM_REPLACESEL, FALSE, (LPARAM)s);
}

static void
render(void)
{
	Conversation *c = &app.conversations[app.selected];
	wchar_t *text;
	size_t i;
	SetWindowText(app.transcript, L"");
	if(c->count == 0)
		append(L"LTI  /  LOCAL AI COMMUNICATOR\r\n\r\nSelect a module and write a message below.\r\n\r\nStart a local model engine before sending.\r\nMessages stay in memory until you close the program.\r\n");
	for(i = 0; i < c->count; i++) {
		append(i % 2 == 0 ? L"YOU\r\n" : L"LTI\r\n");
		text = wide(c->messages[i].text);
		if(text != NULL) { append(text); free(text); }
		append(L"\r\n\r\n");
	}
	SendMessage(app.transcript, EM_SCROLLCARET, 0, 0);
}

static void
busy(int value)
{
	app.busy = value;
	EnableWindow(app.send, !value);
	EnableWindow(app.modules, !value);
	EnableWindow(app.fresh, !value);
	EnableWindow(app.port, !value);
	SendMessage(app.prompt, EM_SETREADONLY, value, 0);
	SetWindowText(app.status, value ? L"Working locally..." : L"Ready  |  Local only  |  Ctrl+Enter sends");
}

static void
workfree(Work *w)
{
	free(w->request);
	free(w->prompt);
	free(w->answer);
	free(w);
}

static DWORD WINAPI
complete(void *arg)
{
	Work *w = arg;
	w->ok = lti_local_engine.complete(w->port, w->request,
	    &w->answer, w->error, sizeof(w->error));
	if(!PostMessage(w->target, ReplyReady, 0, (LPARAM)w))
		workfree(w);
	return 0;
}

static void
submit(void)
{
	wchar_t porttext[16], *end, *input;
	unsigned long port;
	int n;
	HANDLE thread;
	Work *w;
	if(app.busy) return;
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
	if(w->prompt != NULL)
		w->request = conversation_request(&app.conversations[app.selected],
		    &lti_modules[app.selected], w->prompt);
	if(w->request == NULL) {
		SetWindowText(app.status, L"Message or conversation limit reached. Shorten the message or choose New.");
		workfree(w);
		return;
	}
	w->port = (unsigned short)port;
	w->target = app.window;
	busy(1);
	thread = CreateThread(NULL, 0, complete, w, 0, NULL);
	if(thread == NULL) {
		busy(0);
		SetWindowText(app.status, L"Cannot start the local request worker.");
		workfree(w);
	} else CloseHandle(thread);
}

static void
received(Work *w)
{
	Conversation *c = &app.conversations[app.selected];
	wchar_t *answer = w->ok ? wide(w->answer) : NULL;
	wchar_t *error;
	size_t before = c->count, bytes = c->bytes;
	busy(0);
	if(answer != NULL && conversation_add(c, "user", w->prompt) &&
	    conversation_add(c, "assistant", w->answer)) {
		SetWindowText(app.prompt, L"");
		render();
	} else {
		while(c->count > before) free(c->messages[--c->count].text);
		c->bytes = bytes;
		error = wide(w->error[0] ? w->error : "Cannot store answer: invalid UTF-8 or out of memory.");
		SetWindowText(app.status, error ? error : L"Request failed.");
		free(error);
	}
	free(answer);
	workfree(w);
	SetFocus(app.prompt);
}

static HWND
control(const wchar_t *kind, const wchar_t *text, DWORD style, int id)
{
	HWND w = CreateWindowEx(0, kind, text, WS_CHILD | WS_VISIBLE | style,
	    0, 0, 1, 1, app.window, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
	SendMessage(w, WM_SETFONT, (WPARAM)app.normal, TRUE);
	return w;
}

static void
place(HWND w, int x, int y, int width, int height)
{
	MoveWindow(w, px(x), px(y), px(width), px(height), TRUE);
}

static void
layout(void)
{
	RECT r;
	int width, height;
	GetClientRect(app.window, &r);
	width = MulDiv(r.right, 96, app.dpi);
	height = MulDiv(r.bottom, 96, app.dpi);
	place(app.title, 18, 13, 90, 32);
	place(app.subtitle, 112, 23, width - 130, 24);
	place(app.modulelabel, 18, 66, 130, 20);
	place(app.modules, 18, 90, 140, height - 190);
	place(app.portlabel, 18, height - 84, 140, 20);
	place(app.port, 18, height - 60, 78, 24);
	place(app.fresh, 106, height - 60, 52, 24);
	place(app.transcript, 174, 66, width - 192, height - 250);
	place(app.promptlabel, 174, height - 175, width - 192, 20);
	place(app.prompt, 174, height - 151, width - 282, 115);
	place(app.send, width - 96, height - 151, 78, 30);
	place(app.status, 18, height - 24, width - 36, 20);
}

static LRESULT CALLBACK
windowproc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
	switch(message) {
	case WM_SIZE:
		if(app.title != NULL) layout();
		return 0;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lp)->ptMinTrackSize.x = px(640);
		((MINMAXINFO *)lp)->ptMinTrackSize.y = px(480);
		return 0;
	case WM_COMMAND:
		switch(LOWORD(wp)) {
		case IdSend: submit(); break;
		case IdNew:
			if(!app.busy) {
				conversation_clear(&app.conversations[app.selected]);
				SetWindowText(app.prompt, L""); render(); busy(0);
			}
			break;
		case IdModules:
			if(HIWORD(wp) == LBN_SELCHANGE && !app.busy) {
				LRESULT i = SendMessage(app.modules, LB_GETCURSEL, 0, 0);
				if(i >= 0 && (size_t)i < lti_module_count) {
					app.selected = (size_t)i; render();
				}
			}
			break;
		case IdExit: DestroyWindow(window); break;
		case IdAbout:
			MessageBox(window, L"LTI AI 0.1\nA small, native local AI communicator.\n\nC / Win32 / replaceable local engine\nModules are instruction presets, not separate models.\n\nFoundation build: text chat, memory-only sessions.", L"About LTI", MB_OK);
			break;
		}
		return 0;
	case ReplyReady: received((Work *)lp); return 0;
	case WM_DESTROY: PostQuitMessage(0); return 0;
	}
	return DefWindowProc(window, message, wp, lp);
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
	WNDCLASS wc = {0};
	MSG msg;
	HDC dc;
	HMENU menu, file, help;
	size_t i;
	wchar_t *name;
	(void)previous; (void)command;
	SetProcessDPIAware();
	dc = GetDC(NULL); app.dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc);
	wc.lpfnWndProc = windowproc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"LTICommunicator";
	if(!RegisterClass(&wc)) return 1;
	app.normal = CreateFont(-px(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Tahoma");
	app.fixed = CreateFont(-px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH, L"Consolas");
	app.heading = CreateFont(-px(30), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
	menu = CreateMenu(); file = CreatePopupMenu(); help = CreatePopupMenu();
	AppendMenu(file, MF_STRING, IdNew, L"&New conversation");
	AppendMenu(file, MF_STRING, IdExit, L"E&xit");
	AppendMenu(help, MF_STRING, IdAbout, L"&About LTI");
	AppendMenu(menu, MF_POPUP, (UINT_PTR)file, L"&File");
	AppendMenu(menu, MF_POPUP, (UINT_PTR)help, L"&Help");
	app.window = CreateWindow(wc.lpszClassName, L"LTI - Local AI Communicator",
	    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
	    px(960), px(700), NULL, menu, instance, NULL);
	if(app.window == NULL) return 1;
	app.title = control(L"STATIC", L"LTI", 0, 0);
	SendMessage(app.title, WM_SETFONT, (WPARAM)app.heading, TRUE);
	app.subtitle = control(L"STATIC", L"LOCAL AI COMMUNICATOR", 0, 0);
	app.modulelabel = control(L"STATIC", L"&Modules", 0, 0);
	app.modules = control(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | LBS_NOTIFY, IdModules);
	for(i = 0; i < lti_module_count; i++) {
		name = wide(lti_modules[i].name);
		if(name != NULL) { SendMessage(app.modules, LB_ADDSTRING, 0, (LPARAM)name); free(name); }
	}
	SendMessage(app.modules, LB_SETCURSEL, 0, 0);
	app.transcript = control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL |
	    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0);
	SendMessage(app.transcript, WM_SETFONT, (WPARAM)app.fixed, TRUE);
	SendMessage(app.transcript, EM_SETLIMITTEXT, 300000, 0);
	app.promptlabel = control(L"STATIC", L"&Message", 0, 0);
	app.prompt = control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL |
	    ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, IdPrompt);
	SendMessage(app.prompt, EM_SETLIMITTEXT, LtiMaxPrompt / 3, 0);
	SendMessage(app.prompt, WM_SETFONT, (WPARAM)app.fixed, TRUE);
	app.send = control(L"BUTTON", L"&Send", WS_TABSTOP | BS_DEFPUSHBUTTON, IdSend);
	app.portlabel = control(L"STATIC", L"Local engine &port", 0, 0);
	app.port = control(L"EDIT", L"8080", WS_TABSTOP | WS_BORDER | ES_NUMBER, IdPort);
	SendMessage(app.port, EM_SETLIMITTEXT, 5, 0);
	app.fresh = control(L"BUTTON", L"&New", WS_TABSTOP, IdNew);
	app.status = control(L"STATIC", L"", SS_LEFT, 0);
	if(!app.modules || !app.transcript || !app.prompt || !app.send || !app.port) return 1;
	layout(); render(); busy(0);
	ShowWindow(app.window, show); UpdateWindow(app.window); SetFocus(app.prompt);
	while(GetMessage(&msg, NULL, 0, 0) > 0) {
		if(msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
		    (GetKeyState(VK_CONTROL) & 0x8000)) { submit(); continue; }
		if(!IsDialogMessage(app.window, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
	}
	/* A pending worker only owns its Work record; process exit cancels its I/O. */
	for(i = 0; i < lti_module_count; i++) conversation_clear(&app.conversations[i]);
	DeleteObject(app.normal); DeleteObject(app.fixed); DeleteObject(app.heading);
	return 0;
}

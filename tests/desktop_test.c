/* Isolated, hidden controls. No desktop input, real engine, or user data. */
#include "store_win.h"
static int fixture_messagebox(HWND window,LPCWSTR text,LPCWSTR title,UINT flags);
#define MessageBoxW fixture_messagebox
#define local_prepare fixture_prepare
#define local_stream_report fixture_stream
#define WinMain unused_app_entry
#include "../src/winmain.c"
#include <assert.h>

static int replymode;
static int confirmation=IDNO;
static UINT confirmation_flags;
static int fixture_messagebox(HWND window,LPCWSTR text,LPCWSTR title,UINT flags)
{
    (void)window; (void)text; (void)title;
    confirmation_flags=flags; return confirmation;
}
int fixture_prepare(unsigned short port,const wchar_t *model,const Conversation *conversation,
    const Module *module,const char *prompt,const Generation *settings,HANDLE cancel,
    char **request,ContextBudget *budget,char *error,size_t capacity)
{
    (void)port; (void)model; (void)conversation; (void)module; (void)prompt;
    (void)settings; (void)cancel; (void)budget;
    if(!replymode) { snprintf(error,capacity,"Fixture failure. Your draft was retained."); return 0; }
    *request=malloc(3); assert(*request); strcpy(*request,"{}"); return 1;
}
int fixture_stream(unsigned short port,const char *body,HANDLE cancel,StreamUpdate update,
    void *context,char **answer,ReplyReport *report,char *error,size_t capacity)
{
    const char *text="**Formatted** reply";
    (void)port; (void)body;
    if(replymode==2) { SetEvent(cancel); return 2; }
    *answer=malloc(strlen(text)+1); assert(*answer); strcpy(*answer,text);
    update(text,context); report->finish=FinishStop;
    if(replymode==3) { snprintf(error,capacity,"Fixture interrupted"); return 0; }
    return 1;
}
static void drain(void)
{
    MSG msg;
    ULONGLONG deadline=GetTickCount64()+5000;
    while(app.busy && GetTickCount64()<deadline) {
        while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(1);
    }
    assert(!app.busy);
}
static void expectprompt(const wchar_t *expected)
{
    wchar_t text[256]; GetWindowTextW(app.prompt,text,256);
    if(wcscmp(text,expected)) fwprintf(stderr,L"Mode %d, busy %d: expected [%ls], got [%ls]\n",replymode,app.busy,expected,text);
    assert(!wcscmp(text,expected));
}
static void draft(const wchar_t *text)
{
    SetWindowTextW(app.prompt,text);
    SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(IdPrompt,EN_CHANGE),(LPARAM)app.prompt);
}
static void cleanfolder(const wchar_t *root,const wchar_t *folder)
{
    wchar_t path[MAX_PATH],pattern[MAX_PATH]; WIN32_FIND_DATAW data;
    HANDLE find;
    swprintf(pattern,MAX_PATH,L"%ls\\%ls\\*",root,folder);
    find=FindFirstFileW(pattern,&data); assert(find!=INVALID_HANDLE_VALUE);
    do {
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        swprintf(path,MAX_PATH,L"%ls\\%ls\\%ls",root,folder,data.cFileName); assert(DeleteFileW(path));
    } while(FindNextFileW(find,&data));
    FindClose(find); swprintf(path,MAX_PATH,L"%ls\\%ls",root,folder); assert(RemoveDirectoryW(path));
}
int main(void)
{
    WNDCLASSW wc={0};
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_TREEVIEW_CLASSES};
    Workspace workspace={0}; Chat check={0};
    wchar_t temp[MAX_PATH],root[MAX_PATH],path[MAX_PATH],text[256];
    char first[33];
    size_t count;
    LVITEMW item={0}; LVCOLUMNW column={0};
    CHARFORMAT2W format={0};
    assert(GetTempPathW(MAX_PATH,temp));
    swprintf(root,MAX_PATH,L"%lsLCB-desktop-%lu-%llu",temp,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    assert(store_open(&app.store,root));
    swprintf(app.config,MAX_PATH,L"%ls\\test.ini",root);
    strcpy(workspace.name,"Fixture"); assert(store_workspace(&app.store,&workspace));
    assert(store_new(&app.store,workspace.id,&app.chat)); strcpy(first,app.chat.info.id);
    InitCommonControlsEx(&common); app.rich=LoadLibraryW(L"Msftedit.dll"); assert(app.rich);
    wc.lpfnWndProc=windowproc; wc.hInstance=GetModuleHandleW(NULL); wc.lpszClassName=L"LCBHiddenFixture";
    assert(RegisterClassW(&wc)); app.dpi=96; app.property=-1; app.leftsize=210; app.rightsize=280; app.outputsize=128;
    app.window=CreateWindowW(wc.lpszClassName,L"Fixture",WS_OVERLAPPEDWINDOW,0,0,1240,820,NULL,NULL,wc.hInstance,NULL);
    assert(app.window && !IsWindowVisible(app.window));
    app.transcript=control(MSFTEDIT_CLASS,L"",ES_MULTILINE|ES_READONLY,0);
    app.prompt=control(L"EDIT",L"",ES_MULTILINE,IdPrompt);
    app.activity=control(L"EDIT",L"",ES_MULTILINE,0);
    app.port=control(L"EDIT",L"8080",0,IdPort);
    app.exchange=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,IdExchange);
    app.chattabs=control(WC_TABCONTROLW,L"",0,IdChatTabs);
    app.modules=control(WC_TREEVIEWW,L"",0,IdModules);
    app.models=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL,IdModels);
    app.details=control(WC_LISTVIEWW,L"",LVS_REPORT,IdDetails);
    column.mask=LVCF_TEXT|LVCF_WIDTH; column.pszText=L"Name"; column.cx=120;
    ListView_InsertColumn(app.models,0,&column); ListView_InsertColumn(app.details,0,&column);
    column.pszText=L"Size"; ListView_InsertColumn(app.models,1,&column); ListView_InsertColumn(app.details,1,&column);
    showchat(); count=app.store.nchats;
    closetab(); assert(!app.ntabs && app.store.nchats==count);
    assert(!TreeView_GetSelection(app.modules));
    assert(!IsWindowEnabled(app.prompt));
    SendMessageW(app.window,WM_COMMAND,IdNew,0);
    assert(app.ntabs==1 && app.store.nchats==count && !strcmp(first,app.chat.info.id));
    draft(L"Retained draft"); assert(savechat());
    closetab(); assert(!app.ntabs && app.store.nchats==count); expectprompt(L"");
    activatechat(first); expectprompt(L"Retained draft");
    assert(store_load(&app.store,first,&check) && !strcmp(check.draft,"Retained draft")); chat_clear(&check);
    app.health=EngineReady; app.settings=generation_defaults();
    replymode=0; submit(); assert(app.busy); expectprompt(L"");
    assert(!strcmp(app.chat.draft,"Retained draft")); drain(); expectprompt(L"Retained draft");
    replymode=2; submit(); expectprompt(L""); drain(); expectprompt(L"Retained draft");
    replymode=1; submit(); expectprompt(L""); drain(); expectprompt(L"");
    assert(app.chat.conversation.count==2 && !app.chat.draft[0]);
    assert(store_load(&app.store,first,&check) && check.conversation.count==2 && !check.draft[0]); chat_clear(&check);
    draft(L"Partial answer"); replymode=3; submit(); expectprompt(L""); drain();
    assert(app.chat.conversation.count==4 && app.chat.conversation.messages[3].status==AnswerError); expectprompt(L"");
    SetWindowTextW(app.transcript,L""); markdown_render(L"**Bold** plain `code`",markdownspan,NULL);
    GetWindowTextW(app.transcript,text,256); assert(!wcscmp(text,L"Bold plain code"));
    format.cbSize=sizeof(format); SendMessageW(app.transcript,EM_SETSEL,0,4);
    SendMessageW(app.transcript,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format); assert(format.dwEffects&CFE_BOLD);
    SendMessageW(app.transcript,EM_SETSEL,5,10); SendMessageW(app.transcript,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    assert(!(format.dwEffects&(CFE_BOLD|CFE_ITALIC)));
    app.library=calloc(1,sizeof(*app.library)); assert(app.library); app.library->count=2;
    wcscpy(app.library->models[0].name,L"Alpha"); app.library->models[0].bytes=200;
    wcscpy(app.library->models[1].name,L"Zeta"); app.library->models[1].bytes=100;
    item.mask=LVIF_TEXT|LVIF_PARAM; item.pszText=L"Alpha"; item.lParam=0; ListView_InsertItem(app.models,&item);
    item.iItem=1; item.pszText=L"Zeta"; item.lParam=1; ListView_InsertItem(app.models,&item);
    ListView_SetItemState(app.models,0,LVIS_SELECTED,LVIS_SELECTED);
    app.sortcolumn=1; sortmodels(); assert(ListView_GetNextItem(app.models,-1,LVNI_SELECTED)==1);
    selectmodel(0); ListView_GetItemText(app.details,0,1,text,256); assert(!wcscmp(text,L"Alpha"));
    app.sortdescending=1; sortmodels(); assert(ListView_GetNextItem(app.models,-1,LVNI_SELECTED)==0);
    {
        char second[33],third[33];
        HANDLE held;
        count=app.store.nchats;
        draft(L"Keep this draft");
        confirmdelete(first);
        assert((confirmation_flags&MB_DEFBUTTON2) && app.store.nchats==count && app.dirty);
        expectprompt(L"Keep this draft");
        swprintf(path,MAX_PATH,L"%ls\\chats\\%hs.json",root,first);
        held=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); assert(held!=INVALID_HANDLE_VALUE);
        confirmation=IDYES; confirmdelete(first);
        assert(app.store.nchats==count && app.ntabs==1 && app.dirty);
        expectprompt(L"Keep this draft"); CloseHandle(held);
        app.busy=1; assert(!deletechat(first)); app.busy=0;
        assert(app.store.nchats==count);
        assert(store_new(&app.store,workspace.id,&check)); strcpy(second,check.info.id); chat_clear(&check);
        activatechat(second); assert(app.ntabs==2);
        draft(L"Other draft");
        assert(deletechat(first));
        assert(app.ntabs==1 && !strcmp(app.chat.info.id,second) && app.dirty);
        expectprompt(L"Other draft");
        assert(store_new(&app.store,workspace.id,&check)); strcpy(third,check.info.id); chat_clear(&check);
        activatechat(third); assert(app.ntabs==2);
        /* Deleting the active tab selects an existing tab and restores its draft. */
        confirmdelete(third);
        assert(app.ntabs==1 && !strcmp(app.chat.info.id,second)); expectprompt(L"Other draft");
        /* A closed cached chat can be deleted without being resurrected by New. */
        closetab(); assert(!app.ntabs);
        assert(deletechat(second) && !app.store.nchats && !app.chat.info.id[0]);
        assert(!app.ntabs && !app.dirty && !IsWindowEnabled(app.prompt));
        assert(!TreeView_GetCount(app.modules) || TreeView_GetCount(app.modules)==1);
        assert(!GetPrivateProfileIntW(L"session",L"tabs",99,app.config));
        assert(savechat() && !app.store.nchats);
        store_close(&app.store); wcscpy(app.dataroot,root);
        assert(openstore() && !app.store.nchats && !app.chat.info.id[0]); showchat();
        assert(!app.ntabs);
        SendMessageW(app.window,WM_COMMAND,IdNew,0);
        assert(app.store.nchats==1 && app.ntabs==1 && app.chat.info.id[0]);
        assert(strcmp(first,app.chat.info.id) && strcmp(second,app.chat.info.id));
        /* Last open tab also leaves an empty workspace, including pending edits. */
        draft(L"Discard with confirmation"); confirmdelete(app.chat.info.id);
        assert(!app.store.nchats && !app.ntabs && !app.dirty); expectprompt(L"");
        /* Startup finds saved chats even when the first workspace is empty. */
        memset(&workspace,0,sizeof(workspace)); strcpy(workspace.name,"Another workspace");
        assert(store_workspace(&app.store,&workspace));
        assert(store_new(&app.store,workspace.id,&check)); strcpy(third,check.info.id); chat_clear(&check);
        store_close(&app.store); assert(openstore());
        assert(!strcmp(app.chat.info.id,third) && app.selected<app.store.nworkspaces);
        showchat(); assert(app.ntabs==1);
    }
    assert(!IsWindowVisible(app.window));
    DestroyWindow(app.window); free(app.library); chat_clear(&app.chat); store_close(&app.store); FreeLibrary(app.rich);
    cleanfolder(root,L"chats"); cleanfolder(root,L"workspaces");
    swprintf(path,MAX_PATH,L"%ls\\session.lock",root); assert(DeleteFileW(path));
    assert(DeleteFileW(app.config)); assert(RemoveDirectoryW(root));
    puts("Desktop regressions passed: tabs, drafts, send outcomes, rich text, model selection, deletion and cancellation."); return 0;
}

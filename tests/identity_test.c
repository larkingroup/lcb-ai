#include "app_paths.h"
#include "store_win.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void)
{
    wchar_t temp[MAX_PATH], base[MAX_PATH], root[MAX_PATH], legacy[MAX_PATH], file[MAX_PATH], selected[MAX_PATH];
    Store old={0}, app={0};
    Workspace w={0};
    Chat chat={0}, loaded={0};
    char id[33];
    assert(GetTempPathW(MAX_PATH,temp));
    assert(GetTempFileNameW(temp,L"lcb",0,base));
    assert(DeleteFileW(base)); assert(CreateDirectoryW(base,NULL));
    assert(app_data_root(base,root));
    swprintf(selected,MAX_PATH,L"%ls\\lcb-ai",base);
    assert(!wcscmp(root,selected));
    assert(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);

    swprintf(legacy,MAX_PATH,L"%ls\\lti-ai",base);
    assert(store_open(&old,legacy));
    strcpy(w.name,"Preserved workspace"); strcpy(w.prompt,"Keep this instruction.");
    assert(store_workspace(&old,&w));
    assert(store_new(&old,w.id,&chat)); strcpy(id,chat.info.id);
    strcpy(chat.draft,"Keep this unsent draft.");
    assert(conversation_add(&chat.conversation,"user","Existing question"));
    assert(conversation_add(&chat.conversation,"assistant","Existing answer"));
    assert(store_save(&old,&chat));
    /* A pre-status chat has only role/content fields. */
    {
        FILE *legacychat;
        swprintf(file,MAX_PATH,L"%ls\\chats\\%hs.json",legacy,id);
        legacychat=_wfopen(file,L"wb"); assert(legacychat);
        assert(fprintf(legacychat,"{\"version\":1,\"id\":\"%s\",\"workspace\":\"%s\","
            "\"title\":\"Legacy chat\",\"draft\":\"Keep this unsent draft.\",\"messages\":["
            "{\"role\":\"user\",\"content\":\"Existing question\"},"
            "{\"role\":\"assistant\",\"content\":\"Existing answer\"}]}",id,w.id)>0);
        assert(!fclose(legacychat));
    }
    swprintf(file,MAX_PATH,L"%ls\\settings.ini",legacy);
    assert(WritePrivateProfileStringW(L"library",L"folder",L"C:\\Models",file));
    assert(app_data_root(base,root)); assert(!wcscmp(root,legacy));
    assert(!store_open(&app,root)); store_close(&app);
    store_close(&old); chat_clear(&chat);
    assert(store_open(&app,root)); assert(store_load(&app,id,&loaded));
    assert(loaded.conversation.count==2 && !strcmp(loaded.draft,"Keep this unsent draft."));
    assert(loaded.conversation.messages[1].status==AnswerUnknown && !loaded.conversation.messages[1].error[0]);
    assert(!strcmp(app.workspaces[0].prompt,"Keep this instruction."));
    GetPrivateProfileStringW(L"library",L"folder",L"",selected,MAX_PATH,file);
    assert(!wcscmp(selected,L"C:\\Models"));
    store_close(&app); chat_clear(&loaded);

    /* New identity wins when deliberately present; LTS precedes older LTI. */
    swprintf(selected,MAX_PATH,L"%ls\\lts-ai",base); assert(CreateDirectoryW(selected,NULL));
    assert(app_data_root(base,root)); assert(!wcscmp(root,selected));
    assert(RemoveDirectoryW(selected));
    swprintf(selected,MAX_PATH,L"%ls\\lcb-ai",base); assert(CreateDirectoryW(selected,NULL));
    assert(app_data_root(base,root)); assert(!wcscmp(root,selected));
    assert(RemoveDirectoryW(selected));
    assert(!app_data_root(L"",root)); assert(!root[0]);

    assert(DeleteFileW(file));
    swprintf(file,MAX_PATH,L"%ls\\chats\\%hs.json",legacy,id); assert(DeleteFileW(file));
    swprintf(file,MAX_PATH,L"%ls\\workspaces\\%hs.json",legacy,w.id); assert(DeleteFileW(file));
    swprintf(file,MAX_PATH,L"%ls\\session.lock",legacy); assert(DeleteFileW(file));
    swprintf(file,MAX_PATH,L"%ls\\chats",legacy); assert(RemoveDirectoryW(file));
    swprintf(file,MAX_PATH,L"%ls\\workspaces",legacy); assert(RemoveDirectoryW(file));
    assert(RemoveDirectoryW(legacy)); assert(RemoveDirectoryW(base));
    puts("Identity tests passed: fresh path, legacy priority, chat/draft/settings preservation, shared lock.");
    return 0;
}

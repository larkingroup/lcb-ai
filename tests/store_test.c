#include "store_win.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
cleanup(const wchar_t *root, const wchar_t *folder)
{
	wchar_t pattern[MAX_PATH], name[MAX_PATH];
	WIN32_FIND_DATAW data;
	HANDLE find;
	swprintf(pattern,MAX_PATH,L"%ls\\%ls\\*",root,folder);
	find=FindFirstFileW(pattern,&data);
	if(find!=INVALID_HANDLE_VALUE) {
		do {
			if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
			swprintf(name,MAX_PATH,L"%ls\\%ls\\%ls",root,folder,data.cFileName);
			assert(DeleteFileW(name));
		} while(FindNextFileW(find,&data));
		FindClose(find);
	}
	swprintf(name,MAX_PATH,L"%ls\\%ls",root,folder); assert(RemoveDirectoryW(name));
}

int
main(void)
{
	Store s={0}, second={0};
	Workspace w={0}, other={0};
	Chat chat={0}, loaded={0}, fresh={0};
	wchar_t temp[MAX_PATH], root[MAX_PATH], file[MAX_PATH], malformed[MAX_PATH];
	char chatid[33], firstworkspace[33], *request;
	cJSON *json, *messages, *content;
	Module module;
	HANDLE held;
	DWORD written;
	assert(GetTempPathW(MAX_PATH,temp)>0);
	assert(GetTempFileNameW(temp,L"lts",0,root));
	assert(DeleteFileW(root));
	assert(store_open(&s,root));
	assert(!store_open(&second,root)); store_close(&second);
	strcpy(w.name,"Workshop \xe2\x98\xba"); strcpy(w.prompt,"Reply in two lines.\nKeep \"quotes\" and accents: caf\xc3\xa9.");
	assert(store_workspace(&s,&w)); strcpy(firstworkspace,w.id);
	assert(store_new(&s,w.id,&chat)); strcpy(chatid,chat.info.id);
	strcpy(chat.info.title,"A \"quoted\" chat"); strcpy(chat.draft,"Unsent draft\r\n\xe2\x98\xba");
	assert(conversation_add(&chat.conversation,"user","Hello\n\xe2\x98\xba"));
	assert(conversation_add(&chat.conversation,"assistant","Reply \"one\"."));
	assert(store_save(&s,&chat));
	assert(store_new(&s,w.id,&fresh));
	assert(strcmp(fresh.info.id,chatid)!=0 && s.nchats==2);
	strcpy(other.name,"Independent"); strcpy(other.prompt,"Other master prompt");
	assert(store_workspace(&s,&other));
	assert(store_new(&s,other.id,&fresh));
	assert(s.nworkspaces==2 && s.nchats==3);
	store_close(&s); chat_clear(&chat); chat_clear(&fresh);
	assert(store_open(&s,root));
	assert(s.nworkspaces==2 && s.nchats==3 && s.skipped==0);
	assert(store_load(&s,chatid,&loaded));
	assert(strcmp(loaded.info.workspace,firstworkspace)==0);
	assert(loaded.conversation.count==2 && loaded.info.turns==1);
	assert(strcmp(loaded.draft,"Unsent draft\r\n\xe2\x98\xba")==0);
	assert(strcmp(loaded.conversation.messages[0].text,"Hello\n\xe2\x98\xba")==0);
	w=*store_find_workspace(&s,firstworkspace);
	assert(strcmp(w.prompt,"Reply in two lines.\nKeep \"quotes\" and accents: caf\xc3\xa9.")==0);
	strcpy(w.prompt,"Changed workspace master prompt"); assert(store_workspace(&s,&w));
	module.name=w.name; module.instruction=store_find_workspace(&s,firstworkspace)->prompt;
	request=conversation_request(&loaded.conversation,&module,"Next"); assert(request);
	json=cJSON_Parse(request); free(request); assert(json);
	messages=cJSON_GetObjectItemCaseSensitive(json,"messages");
	content=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(messages,0),"content");
	assert(cJSON_IsString(content) && strcmp(content->valuestring,w.prompt)==0); cJSON_Delete(json);
	swprintf(file,MAX_PATH,L"%ls\\chats\\%hs.json",root,chatid);
	held=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); assert(held!=INVALID_HANDLE_VALUE);
	strcpy(loaded.draft,"This save must fail");
	assert(!store_save(&s,&loaded));
	CloseHandle(held);
	assert(store_load(&s,chatid,&chat));
	assert(strcmp(chat.draft,"Unsent draft\r\n\xe2\x98\xba")==0);
	assert(!store_load(&s,"..\\escape",&chat));
	assert(chat.conversation.count==2);
	strcpy(loaded.draft,"This save succeeds"); assert(store_save(&s,&loaded));
	assert(store_load(&s,chatid,&chat) && strcmp(chat.draft,"This save succeeds")==0);
	w.prompt[0]=0; assert(store_workspace(&s,&w));
	store_close(&s);
	swprintf(malformed,MAX_PATH,L"%ls\\chats\\ffffffffffffffffffffffffffffffff.json",root);
	held=CreateFileW(malformed,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL); assert(held!=INVALID_HANDLE_VALUE);
	assert(WriteFile(held,"{bad",4,&written,NULL) && written==4); CloseHandle(held);
	assert(store_open(&s,root));
	assert(s.nchats==3 && s.skipped==1);
	assert(store_find_workspace(&s,firstworkspace)->prompt[0]==0);
	assert(GetFileAttributesW(malformed)!=INVALID_FILE_ATTRIBUTES);
	assert(store_load(&s,chatid,&fresh) && fresh.conversation.count==2);
	store_close(&s); chat_clear(&chat); chat_clear(&loaded); chat_clear(&fresh);
	cleanup(root,L"chats"); cleanup(root,L"workspaces");
	swprintf(file,MAX_PATH,L"%ls\\session.lock",root); assert(DeleteFileW(file));
	assert(RemoveDirectoryW(root));
	puts("Storage tests passed: reopen, Unicode, prompts, drafts, isolation, failed replacement, damaged files, locking.");
	return 0;
}

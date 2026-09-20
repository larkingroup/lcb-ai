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

static void
branches(Store *s, const char *workspace)
{
	Chat source={0}, branch={0}, loaded={0};
	char id[33], *request;
	int status;
	size_t count;
	cJSON *json, *messages;
	assert(store_new(s,workspace,&source));
	strcpy(id,source.info.id);
	strcpy(source.info.title,"Conversation"); strcpy(source.draft,"Unsent original draft");
	for(status=AnswerUnknown;status<=AnswerOther;status++) {
		assert(conversation_add(&source.conversation,"user","Original question"));
		assert(conversation_add(&source.conversation,"assistant","Answer: caf\xc3\xa9"));
		source.conversation.messages[source.conversation.count-1].status=status;
		if(status==AnswerError) strcpy(source.conversation.messages[source.conversation.count-1].error,"Engine disconnected.");
	}
	assert(store_save(s,&source));
	assert(store_load(s,id,&loaded));
	for(status=AnswerUnknown;status<=AnswerOther;status++) assert(loaded.conversation.messages[status*2+1].status==status);
	assert(!strcmp(loaded.conversation.messages[AnswerError*2+1].error,"Engine disconnected."));
	assert(store_branch(s,&source,3,"Revised question","Edit",&branch));
	assert(strcmp(branch.info.id,id) && !strcmp(branch.info.workspace,workspace));
	assert(branch.conversation.count==6 && branch.conversation.messages[5].status==AnswerStopped);
	assert(!strcmp(branch.draft,"Revised question"));
	request=conversation_request(&branch.conversation,&lcb_modules[0],branch.draft); assert(request);
	json=cJSON_Parse(request); free(request); assert(json);
	messages=cJSON_GetObjectItem(json,"messages"); assert(cJSON_GetArraySize(messages)==8);
	assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages,7),"content")->valuestring,"Revised question"));
	assert(!cJSON_GetObjectItem(cJSON_GetArrayItem(messages,2),"status")); cJSON_Delete(json);
	assert(store_load(s,branch.info.id,&loaded) && loaded.conversation.count==6);
	assert(!strcmp(loaded.draft,"Revised question"));
	/* Same-object retry copies its prompt before replacing the destination. */
	assert(store_branch(s,&source,0,source.conversation.messages[0].text,"Retry",&source));
	assert(source.conversation.count==0 && !strcmp(source.draft,"Original question"));
	assert(store_load(s,id,&loaded) && loaded.conversation.count==12);
	assert(!strcmp(loaded.draft,"Unsent original draft"));
	assert(!store_branch(s,&loaded,6,"Invalid turn","Edit",&branch));
	assert(branch.conversation.count==6 && !strcmp(branch.draft,"Revised question"));
	/* A failed save must leave both the source and destination intact. */
	count=s->nchats; s->nchats=StoreChats;
	assert(!store_branch(s,&loaded,1,"Unsaved","Edit",&branch)); s->nchats=count;
	assert(branch.conversation.count==6 && !strcmp(branch.draft,"Revised question"));
	assert(loaded.conversation.count==12 && !strcmp(loaded.info.id,id));
	chat_clear(&source); chat_clear(&branch); chat_clear(&loaded);
}

static void deletion(Store *s,const char *workspace)
{
	Chat chat={0},loaded={0};
	char id[33];
	wchar_t file[MAX_PATH];
	HANDLE held;
	size_t count=s->nchats;
	assert(store_new(s,workspace,&chat)); strcpy(id,chat.info.id);
	strcpy(chat.draft,"A draft to delete"); assert(store_save(s,&chat));
	assert(!store_delete(s,"..\\escape"));
	assert(!store_delete(s,"00000000000000000000000000000000"));
	assert(s->nchats==count+1);
	swprintf(file,MAX_PATH,L"%ls\\chats\\%hs.json",s->root,id);
	held=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); assert(held!=INVALID_HANDLE_VALUE);
	assert(!store_delete(s,id) && s->nchats==count+1);
	assert(store_load(s,id,&loaded) && !strcmp(loaded.draft,chat.draft));
	CloseHandle(held);
	/* Allow callers to pass the identifier directly from the compacted index. */
	assert(store_delete(s,s->chats[count].id));
	assert(s->nchats==count && GetFileAttributesW(file)==INVALID_FILE_ATTRIBUTES);
	assert(!store_load(s,id,&loaded) && !strcmp(loaded.draft,chat.draft));
	assert(!store_delete(s,id) && s->nchats==count);
	chat_clear(&chat); chat_clear(&loaded);
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
	assert(GetTempFileNameW(temp,L"lcb",0,root));
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
	/* A long chat exceeds both former limits: 32 messages and 1 MiB JSON.
	   Reopen the whole store too, since its index scan also parses chat files. */
	{
		char text[4097];
		size_t i;
		memset(text,'x',4096); text[4096]=0;
		for(i=0;i<160;i++) {
			assert(conversation_add(&fresh.conversation,"user",text));
			assert(conversation_add(&fresh.conversation,"assistant","Unicode reply: caf\xc3\xa9 \xe2\x98\xba"));
			assert(conversation_add(&fresh.conversation,"user","Another question"));
			assert(conversation_add(&fresh.conversation,"assistant",text));
		}
		assert(fresh.conversation.bytes>1048576 && fresh.conversation.count==642);
		assert(store_save(&s,&fresh));
		store_close(&s); chat_clear(&fresh);
		assert(store_open(&s,root) && s.nchats==3 && s.skipped==1);
		assert(store_load(&s,chatid,&fresh));
		assert(fresh.info.turns==321 && fresh.conversation.count==642);
		assert(!strcmp(fresh.conversation.messages[0].text,"Hello\n\xe2\x98\xba"));
		assert(!strcmp(fresh.conversation.messages[641].text,text));
	}
	branches(&s,firstworkspace);
	deletion(&s,firstworkspace);
	store_close(&s); chat_clear(&chat); chat_clear(&loaded); chat_clear(&fresh);
	cleanup(root,L"chats"); cleanup(root,L"workspaces");
	swprintf(file,MAX_PATH,L"%ls\\session.lock",root); assert(DeleteFileW(file));
	assert(RemoveDirectoryW(root));
	puts("Storage tests passed: reopen, Unicode, prompts, drafts, isolation, failed replacement, damaged files, locking, answer status, retry/edit branches.");
	return 0;
}

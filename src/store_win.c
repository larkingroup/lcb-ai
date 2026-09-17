#include "store_win.h"
#include "cJSON.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int
fail(Store *s, const wchar_t *message)
{
	swprintf(s->error, 512, L"%ls (Windows error %lu).", message, (unsigned long)GetLastError());
	return 0;
}

static int
validid(const char *id)
{
	size_t i;
	if(strlen(id) != 32) return 0;
	for(i=0; i<32; i++) if(!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f'))) return 0;
	return 1;
}

static int
newid(char *id)
{
	unsigned char bytes[16];
	static const char hex[] = "0123456789abcdef";
	size_t i;
	if(BCryptGenRandom(NULL, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return 0;
	for(i=0; i<16; i++) { id[i*2]=hex[bytes[i]>>4]; id[i*2+1]=hex[bytes[i]&15]; }
	id[32]=0; return 1;
}

static int
validtext(const char *text, size_t max, int empty)
{
	size_t n = strlen(text);
	return n<=max && (empty || n>0) && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0)>0;
}

static int
field(cJSON *json, const char *key, char *to, size_t capacity, int empty)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(json, key);
	if(!cJSON_IsString(v) || !validtext(v->valuestring, capacity-1, empty)) return 0;
	strcpy(to, v->valuestring); return 1;
}

static void
path(Store *s, wchar_t *out, const wchar_t *folder, const char *id)
{
	swprintf(out, MAX_PATH, L"%ls\\%ls\\%hs.json", s->root, folder, id);
}

static int
directory(const wchar_t *name)
{
	DWORD a;
	if(CreateDirectoryW(name, NULL)) return 1;
	a = GetFileAttributesW(name);
	return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY) && !(a&FILE_ATTRIBUTE_REPARSE_POINT);
}

static cJSON *
readjson(const wchar_t *name)
{
	HANDLE f;
	LARGE_INTEGER size;
	DWORD got;
	char *data;
	cJSON *json = NULL;
	f=CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(f==INVALID_HANDLE_VALUE) return NULL;
	if(!GetFileSizeEx(f,&size) || size.QuadPart<=0 || size.QuadPart>LtsMaxWire) { CloseHandle(f); return NULL; }
	data=malloc((size_t)size.QuadPart+1);
	if(data && ReadFile(f,data,(DWORD)size.QuadPart,&got,NULL) && got==(DWORD)size.QuadPart && !memchr(data,0,got)) {
		data[got]=0;
		if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,-1,NULL,0)>0)
			json=cJSON_ParseWithLengthOpts(data,(size_t)got+1,NULL,1);
	}
	free(data); CloseHandle(f);
	return json;
}

static int
writejson(Store *s, const wchar_t *name, cJSON *json, int existing)
{
	wchar_t temp[MAX_PATH];
	char *data=cJSON_PrintUnformatted(json), id[33];
	HANDLE f;
	DWORD n, error;
	int ok=0;
	if(!data || strlen(data)>LtsMaxWire || !newid(id)) { free(data); return fail(s,L"Cannot prepare save"); }
	swprintf(temp,MAX_PATH,L"%ls\\%hs.tmp",s->root,id);
	f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
	if(f!=INVALID_HANDLE_VALUE) {
		ok=WriteFile(f,data,(DWORD)strlen(data),&n,NULL) && n==strlen(data) && FlushFileBuffers(f);
		error=GetLastError(); CloseHandle(f); SetLastError(error);
		if(ok) ok=MoveFileExW(temp,name,MOVEFILE_WRITE_THROUGH | (existing?MOVEFILE_REPLACE_EXISTING:0));
		error=GetLastError();
		if(!ok) DeleteFileW(temp);
		SetLastError(error);
	}
	free(data);
	if(!ok) return fail(s,L"Could not save; previous file retained");
	s->error[0]=0; return 1;
}

Workspace *
store_find_workspace(Store *s, const char *id)
{
	size_t i;
	for(i=0;i<s->nworkspaces;i++) if(strcmp(s->workspaces[i].id,id)==0) return &s->workspaces[i];
	return NULL;
}

static int
workspace_parse(cJSON *json, Workspace *w)
{
	cJSON *v=cJSON_GetObjectItemCaseSensitive(json,"version");
	return cJSON_IsNumber(v) && v->valuedouble==1 &&
	    field(json,"id",w->id,sizeof(w->id),0) && validid(w->id) &&
	    field(json,"name",w->name,sizeof(w->name),0) &&
	    field(json,"master_prompt",w->prompt,sizeof(w->prompt),1);
}

static int
chat_parse(cJSON *json, Chat *chat)
{
	cJSON *v=cJSON_GetObjectItemCaseSensitive(json,"version"), *messages, *m, *role, *text;
	size_t i=0;
	if(!cJSON_IsNumber(v) || v->valuedouble!=1 ||
	    !field(json,"id",chat->info.id,sizeof(chat->info.id),0) || !validid(chat->info.id) ||
	    !field(json,"workspace",chat->info.workspace,sizeof(chat->info.workspace),0) || !validid(chat->info.workspace) ||
	    !field(json,"title",chat->info.title,sizeof(chat->info.title),0) ||
	    !field(json,"draft",chat->draft,sizeof(chat->draft),1)) return 0;
	messages=cJSON_GetObjectItemCaseSensitive(json,"messages");
	if(!cJSON_IsArray(messages) || cJSON_GetArraySize(messages)>LtsMaxMessages || cJSON_GetArraySize(messages)%2) return 0;
	cJSON_ArrayForEach(m,messages) {
		role=cJSON_GetObjectItemCaseSensitive(m,"role"); text=cJSON_GetObjectItemCaseSensitive(m,"content");
		if(!cJSON_IsString(role) || !cJSON_IsString(text) || !validtext(text->valuestring,LtsMaxReply,0) ||
		    strcmp(role->valuestring,i%2?"assistant":"user")!=0 ||
		    !conversation_add(&chat->conversation,i%2?"assistant":"user",text->valuestring)) return 0;
		i++;
	}
	chat->info.turns=chat->conversation.count/2;
	return 1;
}

void
chat_clear(Chat *chat)
{
	conversation_clear(&chat->conversation); memset(chat,0,sizeof(*chat));
}

int
store_load(Store *s, const char *id, Chat *chat)
{
	wchar_t name[MAX_PATH];
	cJSON *json;
	Chat *next;
	int ok;
	if(!validid(id)) return fail(s,L"Invalid chat identifier");
	path(s,name,L"chats",id); json=readjson(name);
	next=calloc(1,sizeof(*next));
	ok=next && json && chat_parse(json,next) && strcmp(next->info.id,id)==0 && store_find_workspace(s,next->info.workspace);
	cJSON_Delete(json);
	if(ok) { chat_clear(chat); *chat=*next; free(next); return 1; }
	if(next) { chat_clear(next); free(next); }
	return fail(s,L"Cannot read chat; file left unchanged");
}

int
store_workspace(Store *s, Workspace *w)
{
	wchar_t name[MAX_PATH];
	Workspace *old;
	cJSON *json;
	int ok;
	if(!validtext(w->name,StoreName-1,0) || !validtext(w->prompt,LtsMaxPrompt,1)) return fail(s,L"Invalid workspace text");
	if(!w->id[0] && !newid(w->id)) return fail(s,L"Cannot create workspace identifier");
	if(!validid(w->id)) return fail(s,L"Invalid workspace identifier");
	old=store_find_workspace(s,w->id);
	if(!old && s->nworkspaces==StoreWorkspaces) return fail(s,L"Workspace limit reached (64)");
	json=cJSON_CreateObject();
	ok=json && cJSON_AddNumberToObject(json,"version",1) && cJSON_AddStringToObject(json,"id",w->id) &&
	    cJSON_AddStringToObject(json,"name",w->name) && cJSON_AddStringToObject(json,"master_prompt",w->prompt);
	path(s,name,L"workspaces",w->id);
	if(ok) ok=writejson(s,name,json,old!=NULL);
	else fail(s,L"Cannot prepare workspace save");
	cJSON_Delete(json);
	if(ok) { if(old) *old=*w; else s->workspaces[s->nworkspaces++]=*w; }
	return ok;
}

int
store_save(Store *s, Chat *chat)
{
	wchar_t name[MAX_PATH];
	cJSON *json, *messages, *m;
	size_t i, index;
	int ok;
	if(!validid(chat->info.id) || !store_find_workspace(s,chat->info.workspace) ||
	    !validtext(chat->info.title,StoreName-1,0) || !validtext(chat->draft,LtsMaxPrompt,1)) return fail(s,L"Invalid chat");
	for(index=0;index<s->nchats;index++) if(strcmp(s->chats[index].id,chat->info.id)==0) break;
	if(index==s->nchats && s->nchats==StoreChats) return fail(s,L"Chat limit reached (1024)");
	if(chat->conversation.count%2) return fail(s,L"Incomplete conversation");
	json=cJSON_CreateObject();
	messages=json?cJSON_AddArrayToObject(json,"messages"):NULL;
	ok=messages && cJSON_AddNumberToObject(json,"version",1) && cJSON_AddStringToObject(json,"id",chat->info.id) &&
	    cJSON_AddStringToObject(json,"workspace",chat->info.workspace) && cJSON_AddStringToObject(json,"title",chat->info.title) &&
	    cJSON_AddStringToObject(json,"draft",chat->draft);
	for(i=0;ok && i<chat->conversation.count;i++) {
		Message *message=&chat->conversation.messages[i];
		m=cJSON_CreateObject();
		ok=validtext(message->text,LtsMaxReply,0) && strcmp(message->role,i%2?"assistant":"user")==0 && m &&
		    cJSON_AddStringToObject(m,"role",message->role) && cJSON_AddStringToObject(m,"content",message->text);
		if(ok) ok=cJSON_AddItemToArray(messages,m);
		if(!ok) cJSON_Delete(m);
	}
	path(s,name,L"chats",chat->info.id);
	if(ok) ok=writejson(s,name,json,index<s->nchats);
	else fail(s,L"Cannot prepare chat save");
	cJSON_Delete(json);
	if(ok) { chat->info.turns=chat->conversation.count/2; s->chats[index]=chat->info; if(index==s->nchats) s->nchats++; }
	return ok;
}

int
store_new(Store *s, const char *workspace, Chat *chat)
{
	Chat *next=calloc(1,sizeof(*next));
	int ok=0;
	if(next && validid(workspace) && newid(next->info.id)) {
		strcpy(next->info.workspace,workspace); strcpy(next->info.title,"New chat");
		ok=store_save(s,next);
		if(ok) { chat_clear(chat); *chat=*next; }
	} else fail(s,L"Cannot prepare new chat");
	free(next); return ok;
}

static int
scan(Store *s, int chats)
{
	wchar_t pattern[MAX_PATH], name[MAX_PATH];
	WIN32_FIND_DATAW data;
	HANDLE find;
	cJSON *json;
	Workspace *w=calloc(1,sizeof(*w));
	Chat *chat=calloc(1,sizeof(*chat));
	char id[33];
	int ok;
	if(!w || !chat) { free(w); free(chat); return fail(s,L"Cannot load saved data"); }
	swprintf(pattern,MAX_PATH,L"%ls\\%ls\\*.json",s->root,chats?L"chats":L"workspaces");
	find=FindFirstFileW(pattern,&data);
	if(find==INVALID_HANDLE_VALUE) {
		DWORD error=GetLastError(); free(w); free(chat);
		return error==ERROR_FILE_NOT_FOUND ? 1 : fail(s,L"Cannot list saved data");
	}
	do {
		if(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) { s->skipped++; continue; }
		ok=wcslen(data.cFileName)==37 && WideCharToMultiByte(CP_UTF8,0,data.cFileName,32,id,32,NULL,NULL)==32;
		id[32]=0;
		if(!ok || !validid(id)) { s->skipped++; continue; }
		path(s,name,chats?L"chats":L"workspaces",id); json=readjson(name);
		if(chats) {
			ok=json && chat_parse(json,chat) && strcmp(chat->info.id,id)==0 && store_find_workspace(s,chat->info.workspace);
			if(ok && s->nchats<StoreChats) s->chats[s->nchats++]=chat->info;
			else s->skipped++;
			chat_clear(chat);
		} else {
			ok=json && workspace_parse(json,w) && strcmp(w->id,id)==0;
			if(ok && s->nworkspaces<StoreWorkspaces) s->workspaces[s->nworkspaces++]=*w;
			else s->skipped++;
		}
		cJSON_Delete(json);
	} while(FindNextFileW(find,&data));
	FindClose(find); free(w); free(chat); return 1;
}

int
store_open(Store *s, const wchar_t *root)
{
	wchar_t name[MAX_PATH];
	memset(s,0,sizeof(*s));
	if(wcslen(root)>MAX_PATH-80) return fail(s,L"Storage path is too long");
	wcscpy(s->root,root);
	if(!directory(root)) return fail(s,L"Cannot open lts-ai storage folder");
	swprintf(name,MAX_PATH,L"%ls\\session.lock",root);
	s->lock=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if(s->lock==INVALID_HANDLE_VALUE) { s->lock=NULL; return fail(s,L"Storage is in use or unavailable; close the other lts-ai window"); }
	s->workspaces=calloc(StoreWorkspaces,sizeof(*s->workspaces)); s->chats=calloc(StoreChats,sizeof(*s->chats));
	if(!s->workspaces || !s->chats) goto failed;
	swprintf(name,MAX_PATH,L"%ls\\workspaces",root); if(!directory(name)) goto failed;
	swprintf(name,MAX_PATH,L"%ls\\chats",root); if(!directory(name)) goto failed;
	if(scan(s,0) && scan(s,1)) return 1;
failed:
	if(!s->error[0]) fail(s,L"Cannot initialize local storage");
	store_close(s); return 0;
}

void
store_close(Store *s)
{
	if(s->lock) CloseHandle(s->lock);
	free(s->workspaces); free(s->chats);
	s->lock=NULL; s->workspaces=NULL; s->chats=NULL; s->nworkspaces=0; s->nchats=0;
}

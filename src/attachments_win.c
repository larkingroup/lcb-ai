#include "attachments_win.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <limits.h>

typedef struct Media { const char *ext, *kind, *mime; } Media;
/* The Linux build's table, so saved chats validate on both. Attach inserts
 * text files into the message here, so only media is sent as parts. */
static const Media media[] = {
	{".png","vision","image/png"}, {".jpg","vision","image/jpeg"}, {".jpeg","vision","image/jpeg"},
	{".bmp","vision","image/bmp"}, {".gif","vision","image/gif"},
	{".wav","audio","audio/wav"}, {".mp3","audio","audio/mpeg"}, {".flac","audio","audio/flac"},
	{".mp4","video","video/mp4"}, {".mkv","video","video/x-matroska"},
	{".webm","video","video/webm"}, {".mov","video","video/quicktime"},
	{".txt","text",""}, {".md","text",""}, {".markdown","text",""}, {".csv","text",""},
	{".tsv","text",""}, {".json","text",""}, {".jsonl","text",""}, {".xml","text",""},
	{".yaml","text",""}, {".yml","text",""}, {".toml","text",""}, {".ini","text",""},
	{".cfg","text",""}, {".conf","text",""}, {".log","text",""}, {".html","text",""},
	{".htm","text",""}, {".css","text",""}, {".js","text",""}, {".mjs","text",""},
	{".ts","text",""}, {".tsx","text",""}, {".jsx","text",""}, {".py","text",""},
	{".c","text",""}, {".h","text",""}, {".cpp","text",""}, {".hpp","text",""},
	{".cc","text",""}, {".cs","text",""}, {".java","text",""}, {".kt","text",""},
	{".go","text",""}, {".rs","text",""}, {".rb","text",""}, {".php","text",""},
	{".sh","text",""}, {".bash","text",""}, {".ps1","text",""}, {".bat","text",""},
	{".sql","text",""}, {".lua","text",""}, {".swift","text",""}, {".r","text",""},
	{".tex","text",""}, {".srt","text",""}, {".vtt","text",""},
};
enum { MaxMedia = 16*1048576, MaxText = 192*1024, MaxMediaTotal = 46*1048576, MaxMediaWire = 64*1048576 };

static const Media *
find(const char *ext)
{
	size_t i;
	for(i=0; i<sizeof(media)/sizeof(media[0]); i++) if(!strcmp(media[i].ext,ext)) return &media[i];
	return NULL;
}

static int
utf8text(const char *s, size_t max)
{
	size_t n=strlen(s);
	return n>0 && n<=max && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0)>0;
}

static const Media *
validitem(const cJSON *a)
{
	const cJSON *file=cJSON_GetObjectItemCaseSensitive(a,"file"), *name=cJSON_GetObjectItemCaseSensitive(a,"name");
	const cJSON *kind=cJSON_GetObjectItemCaseSensitive(a,"kind"), *size=cJSON_GetObjectItemCaseSensitive(a,"size");
	const Media *m;
	const char *dot;
	size_t i;
	if(!cJSON_IsString(file) || !cJSON_IsString(name) || !cJSON_IsString(kind) || !cJSON_IsNumber(size)) return NULL;
	dot=strchr(file->valuestring,'.');
	if(!dot || dot-file->valuestring!=64 || !(m=find(dot)) || strcmp(kind->valuestring,m->kind)) return NULL;
	for(i=0; i<64; i++) {
		char c=file->valuestring[i];
		if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return NULL;
	}
	if(size->valuedouble<1 || size->valuedouble>(strcmp(m->kind,"text")?MaxMedia:MaxText) ||
	    size->valuedouble!=(double)(size_t)size->valuedouble || !utf8text(name->valuestring,1024)) return NULL;
	return m;
}

int
attach_valid(const cJSON *items)
{
	const cJSON *a;
	if(!cJSON_IsArray(items) || cJSON_GetArraySize(items)>AttachMax) return 0;
	cJSON_ArrayForEach(a,items) if(!validitem(a)) return 0;
	return 1;
}

static cJSON *
parse(const char *list)
{
	cJSON *items=list ? cJSON_Parse(list) : cJSON_CreateArray();
	if(items && !attach_valid(items)) { cJSON_Delete(items); return NULL; }
	return items;
}

int
attach_count(const char *list)
{
	cJSON *items=parse(list);
	int n=items ? cJSON_GetArraySize(items) : 0;
	cJSON_Delete(items); return n;
}

int
attach_media_count(const char *list)
{
	cJSON *items=parse(list), *a;
	int n=0;
	if(items) cJSON_ArrayForEach(a,items) if(strcmp(validitem(a)->kind,"text")) n++;
	cJSON_Delete(items); return n;
}

/* The lower-case extension of a path as UTF-8, with its dot. */
static int
extension(const wchar_t *path, char *ext, size_t capacity)
{
	const wchar_t *dot=wcsrchr(path,L'.'), *slash=wcsrchr(path,L'\\');
	wchar_t lower[16];
	size_t i;
	if(!dot || (slash && dot<slash) || wcslen(dot)>=16) return 0;
	for(i=0; dot[i]; i++) lower[i]=towlower(dot[i]);
	lower[i]=0;
	return WideCharToMultiByte(CP_UTF8,0,lower,-1,ext,(int)capacity,NULL,NULL)>0;
}

int
attach_is_text(const wchar_t *path)
{
	char ext[64];
	const Media *m=extension(path,ext,sizeof(ext)) ? find(ext) : NULL;
	return m && !strcmp(m->kind,"text");
}

const wchar_t *
attach_filter(void)
{
	return L"Images, audio and video\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.webm;*.mov\0"
	    L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0Audio\0*.wav;*.mp3;*.flac\0Video\0*.mp4;*.mkv;*.webm;*.mov\0";
}

static unsigned char *
readall(const wchar_t *path, size_t limit, size_t *size, wchar_t *error, size_t capacity)
{
	HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	LARGE_INTEGER length;
	unsigned char *data=NULL;
	size_t used=0;
	DWORD n;
	if(file==INVALID_HANDLE_VALUE) { swprintf(error,capacity,L"Cannot open the file (Windows error %lu).",GetLastError()); return NULL; }
	if(!GetFileSizeEx(file,&length)) { swprintf(error,capacity,L"Cannot read the file size."); goto done; }
	if(length.QuadPart<1) { swprintf(error,capacity,L"The file is empty."); goto done; }
	if((unsigned long long)length.QuadPart>limit) { swprintf(error,capacity,L"Attachments are limited to %u MiB each.",(unsigned)(limit/1048576)); goto done; }
	data=malloc((size_t)length.QuadPart);
	if(!data) { swprintf(error,capacity,L"Out of memory."); goto done; }
	while(used<(size_t)length.QuadPart && ReadFile(file,data+used,(DWORD)((size_t)length.QuadPart-used),&n,NULL) && n) used+=n;
	if(used!=(size_t)length.QuadPart) { swprintf(error,capacity,L"The file could not be read completely."); free(data); data=NULL; goto done; }
	*size=used;
done:
	CloseHandle(file); return data;
}

static int
digest(const unsigned char *data, size_t size, char hex[65])
{
	unsigned char sum[32];
	size_t i;
	if(size>ULONG_MAX || BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)data,(ULONG)size,sum,sizeof(sum))) return 0;
	for(i=0; i<32; i++) snprintf(hex+i*2,3,"%02x",sum[i]);
	return 1;
}

static int
storedpath(const wchar_t *root, const char *file, wchar_t path[MAX_PATH])
{
	wchar_t name[80];
	if(!MultiByteToWideChar(CP_UTF8,0,file,-1,name,80)) return 0;
	return swprintf(path,MAX_PATH,L"%ls\\attachments\\%ls",root,name)>0;
}

char *
attach_import(const wchar_t *root, const char *list, const wchar_t *path, wchar_t *error, size_t capacity)
{
	cJSON *items=parse(list), *item=NULL;
	unsigned char *data=NULL;
	size_t size=0;
	char ext[64], hex[65], file[80], *name=NULL, *result=NULL;
	wchar_t folder[MAX_PATH], target[MAX_PATH], partial[MAX_PATH+8];
	const wchar_t *base=wcsrchr(path,L'\\');
	const Media *m=extension(path,ext,sizeof(ext)) ? find(ext) : NULL;
	int length;
	error[0]=0;
	if(!items) { swprintf(error,capacity,L"The saved attachments are invalid."); return NULL; }
	if(!m || !strcmp(m->kind,"text")) { swprintf(error,capacity,L"Choose an image, audio or video file."); goto done; }
	if(cJSON_GetArraySize(items)>=AttachMax) { swprintf(error,capacity,L"Use at most eight attachments per message."); goto done; }
	if(!(data=readall(path,MaxMedia,&size,error,capacity))) goto done;
	if(!digest(data,size,hex)) { swprintf(error,capacity,L"Cannot hash the file."); goto done; }
	snprintf(file,sizeof(file),"%s%s",hex,ext);
	swprintf(folder,MAX_PATH,L"%ls\\attachments",root);
	CreateDirectoryW(folder,NULL);
	if(!storedpath(root,file,target)) { swprintf(error,capacity,L"Storage path is too long."); goto done; }
	/* Identical content is stored once; a new copy lands complete or not at all. */
	if(GetFileAttributesW(target)==INVALID_FILE_ATTRIBUTES) {
		HANDLE out;
		DWORD written;
		int ok;
		swprintf(partial,MAX_PATH+8,L"%ls.part",target);
		out=CreateFileW(partial,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
		if(out==INVALID_HANDLE_VALUE) { swprintf(error,capacity,L"Cannot save the attachment (Windows error %lu).",GetLastError()); goto done; }
		ok=WriteFile(out,data,(DWORD)size,&written,NULL) && written==size && FlushFileBuffers(out);
		CloseHandle(out);
		if(!ok || !MoveFileExW(partial,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
			DeleteFileW(partial); swprintf(error,capacity,L"Cannot save the attachment."); goto done;
		}
	}
	base=base ? base+1 : path;
	length=WideCharToMultiByte(CP_UTF8,0,base,-1,NULL,0,NULL,NULL);
	name=length>0 ? malloc((size_t)length) : NULL;
	if(!name || !WideCharToMultiByte(CP_UTF8,0,base,-1,name,length,NULL,NULL) || !utf8text(name,1024)) {
		swprintf(error,capacity,L"The file name is too long."); goto done;
	}
	item=cJSON_CreateObject();
	if(!item || !cJSON_AddStringToObject(item,"file",file) || !cJSON_AddStringToObject(item,"name",name) ||
	    !cJSON_AddStringToObject(item,"kind",m->kind) || !cJSON_AddNumberToObject(item,"size",(double)size) ||
	    !cJSON_AddItemToArray(items,item)) { cJSON_Delete(item); swprintf(error,capacity,L"Out of memory."); goto done; }
	result=cJSON_PrintUnformatted(items);
	if(!result) swprintf(error,capacity,L"Out of memory.");
done:
	free(data); free(name); cJSON_Delete(items); return result;
}

char *
attach_remove(const char *list, int index)
{
	cJSON *items=parse(list);
	char *result=NULL;
	if(!items) return NULL;
	cJSON_DeleteItemFromArray(items,index);
	if(cJSON_GetArraySize(items)) result=cJSON_PrintUnformatted(items);
	cJSON_Delete(items); return result;
}

int
attach_name(const char *list, int index, wchar_t *name, size_t capacity)
{
	cJSON *items=parse(list);
	cJSON *v=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(items,index),"name");
	int ok=cJSON_IsString(v) && MultiByteToWideChar(CP_UTF8,0,v->valuestring,-1,name,(int)capacity)>0;
	if(!ok && capacity) name[0]=0;
	cJSON_Delete(items); return ok;
}

void
attach_summary(const char *list, wchar_t *text, size_t capacity)
{
	int i, n=attach_count(list);
	size_t used=0;
	wchar_t name[260];
	if(capacity) text[0]=0;
	for(i=0; i<n && used+4<capacity; i++) {
		attach_name(list,i,name,260);
		used+=(size_t)swprintf(text+used,capacity-used,L"%ls%ls",i?L", ":L"",name);
		if(used>=capacity) { text[capacity-1]=0; break; }
	}
}

static char *
base64(const unsigned char *data, size_t size)
{
	static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *out=malloc((size+2)/3*4+1), *p=out;
	size_t i;
	if(!out) return NULL;
	for(i=0; i+2<size; i+=3) {
		unsigned v=(unsigned)data[i]<<16|(unsigned)data[i+1]<<8|data[i+2];
		*p++=table[v>>18]; *p++=table[v>>12&63]; *p++=table[v>>6&63]; *p++=table[v&63];
	}
	if(i<size) {
		unsigned v=(unsigned)data[i]<<16|(i+1<size?(unsigned)data[i+1]<<8:0);
		*p++=table[v>>18]; *p++=table[v>>12&63];
		*p++=i+1<size?table[v>>6&63]:'='; *p++='=';
	}
	*p=0; return out;
}

/* One image_url or input_audio/input_video part from a saved file. */
static cJSON *
mediapart(const wchar_t *root, const cJSON *item, const Media *m, char *error, size_t capacity)
{
	const char *file=cJSON_GetObjectItemCaseSensitive(item,"file")->valuestring;
	const char *name=cJSON_GetObjectItemCaseSensitive(item,"name")->valuestring;
	wchar_t path[MAX_PATH], problem[160];
	unsigned char *data=NULL;
	size_t size=0;
	char hex[65], *encoded=NULL, *url=NULL;
	cJSON *part=NULL, *detail;
	if(!storedpath(root,file,path) || !(data=readall(path,MaxMedia,&size,problem,160)) ||
	    size!=(size_t)cJSON_GetObjectItemCaseSensitive(item,"size")->valuedouble ||
	    !digest(data,size,hex) || strncmp(hex,file,64)) {
		snprintf(error,capacity,"Saved attachment changed or is missing: %s",name); goto done;
	}
	if(!(encoded=base64(data,size))) goto memory;
	part=cJSON_CreateObject();
	if(!part) goto memory;
	if(!strcmp(m->kind,"vision")) {
		size_t n=strlen(m->mime)+strlen(encoded)+16;
		if(!(url=malloc(n))) goto memory;
		snprintf(url,n,"data:%s;base64,%s",m->mime,encoded);
		detail=cJSON_AddObjectToObject(part,"image_url");
		if(!cJSON_AddStringToObject(part,"type","image_url") || !detail || !cJSON_AddStringToObject(detail,"url",url)) goto memory;
	} else {
		char key[16];
		snprintf(key,sizeof(key),"input_%s",m->kind);
		detail=cJSON_AddObjectToObject(part,key);
		if(!cJSON_AddStringToObject(part,"type",key) || !detail || !cJSON_AddStringToObject(detail,"data",encoded) ||
		    !cJSON_AddStringToObject(detail,"format",m->ext+1)) goto memory;
	}
	goto done;
memory:
	snprintf(error,capacity,"Out of memory while preparing attachments.");
	cJSON_Delete(part); part=NULL;
done:
	free(data); free(encoded); free(url); return part;
}

int
attach_request(const wchar_t *root, const Conversation *c, const char *current,
    const cJSON *modalities, HANDLE cancel, char **request, char *error, size_t capacity)
{
	cJSON *json=cJSON_Parse(*request), *turn, *items=NULL, *content=NULL;
	cJSON *messages=cJSON_GetObjectItemCaseSensitive(json,"messages");
	size_t i=0, total=0;
	char *wire=NULL;
	int ok=0;
	error[0]=0;
	if(!cJSON_IsArray(messages)) { snprintf(error,capacity,"Cannot prepare the attachment request."); goto done; }
	cJSON_ArrayForEach(turn,messages) {
		const cJSON *role=cJSON_GetObjectItemCaseSensitive(turn,"role"), *a;
		int last=i>=c->count;
		if(cJSON_IsString(role) && !strcmp(role->valuestring,"system")) continue;
		items=parse(last ? current : c->messages[i].attachments);
		i++;
		if(!items) { snprintf(error,capacity,"A message has invalid saved attachments."); goto done; }
		cJSON_ArrayForEach(a,items) {
			const Media *m=validitem(a);
			cJSON *part, *text;
			if(!strcmp(m->kind,"text")) continue;
			/* Earlier media stays saved, but an engine without it skips it. */
			if(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(modalities,m->kind))) {
				if(!last) continue;
				snprintf(error,capacity,"The loaded engine does not read %s input (%s). Load the model with its media projector.",
				    !strcmp(m->kind,"vision")?"image":m->kind,cJSON_GetObjectItemCaseSensitive(a,"name")->valuestring);
				goto done;
			}
			if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0) { snprintf(error,capacity,"Stopped by user."); goto done; }
			total+=(size_t)cJSON_GetObjectItemCaseSensitive(a,"size")->valuedouble;
			if(total>MaxMediaTotal) { snprintf(error,capacity,"Conversation media exceeds 46 MiB. Start a new chat or use smaller files."); goto done; }
			if(!content) {
				content=cJSON_CreateArray(); text=cJSON_CreateObject();
				if(!content || !text || !cJSON_AddItemToArray(content,text) || !cJSON_AddStringToObject(text,"type","text") ||
				    !cJSON_AddStringToObject(text,"text",cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(turn,"content")))) {
					snprintf(error,capacity,"Out of memory while preparing attachments."); goto done;
				}
			}
			if(!(part=mediapart(root,a,m,error,capacity))) goto done;
			cJSON_AddItemToArray(content,part);
		}
		if(content && !cJSON_ReplaceItemInObjectCaseSensitive(turn,"content",content)) goto done;
		content=NULL; cJSON_Delete(items); items=NULL;
	}
	wire=cJSON_PrintUnformatted(json);
	if(!wire) { snprintf(error,capacity,"Out of memory while preparing attachments."); goto done; }
	if(strlen(wire)>MaxMediaWire) { snprintf(error,capacity,"Media request exceeds 64 MiB."); free(wire); goto done; }
	free(*request); *request=wire; ok=1;
done:
	cJSON_Delete(content); cJSON_Delete(items); cJSON_Delete(json);
	return ok;
}

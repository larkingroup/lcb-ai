#include "library_win.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct Reader {
	FILE *file;
	uint64_t left;
} Reader;

const wchar_t *
model_quant(uint32_t type)
{
	static const wchar_t *names[]={L"F32",L"F16",L"Q4_0",L"Q4_1",L"Legacy",L"Legacy",L"Legacy",
	    L"Q8_0",L"Q5_0",L"Q5_1",L"Q2_K",L"Q3_K_S",L"Q3_K_M",L"Q3_K_L",L"Q4_K_S",L"Q4_K_M",
	    L"Q5_K_S",L"Q5_K_M",L"Q6_K",L"IQ2_XXS",L"IQ2_XS",L"Q2_K_S",L"IQ3_XS",L"IQ3_XXS",
	    L"IQ1_S",L"IQ4_NL",L"IQ3_S",L"IQ3_M",L"IQ2_S",L"IQ2_M",L"IQ4_XS",L"IQ1_M",L"BF16"};
	return type<sizeof(names)/sizeof(names[0])?names[type]:L"Unspecified";
}

static int
take(Reader *r, void *out, uint64_t n)
{
	if(n>r->left) return 0;
	if(out) { if(fread(out,1,(size_t)n,r->file)!=(size_t)n) return 0; }
	else if(_fseeki64(r->file,(int64_t)n,SEEK_CUR)) return 0;
	r->left-=n; return 1;
}

static int
string(Reader *r, char *out, size_t cap)
{
	uint64_t n;
	if(!take(r,&n,8) || n>r->left) return 0;
	if(!out || n>=cap) { if(out) out[0]=0; return take(r,NULL,n); }
	if(!take(r,out,n)) return 0;
	if(memchr(out,0,(size_t)n)) return 0;
	out[n]=0; return 1;
}

static int
skip(Reader *r, uint32_t type)
{
	static const unsigned sizes[]={1,1,2,2,4,4,4,1,0,0,8,8,8};
	uint32_t element;
	uint64_t n,i;
	if(type>12) return 0;
	if(type==8) return string(r,NULL,0);
	if(type!=9) return take(r,NULL,sizes[type]);
	if(!take(r,&element,4) || !take(r,&n,8) || n>1000000 || element>12 || element==9) return 0;
	if(element!=8) return take(r,NULL,n*sizes[element]);
	for(i=0;i<n;i++) if(!string(r,NULL,0)) return 0;
	return 1;
}

int
model_read(const wchar_t *path, ModelInfo *m)
{
	Reader r;
	uint32_t magic,version,type;
	uint64_t tensors,count,i;
	int ok=0;
	int64_t bytes;
	char key[128],value[1024],basename[256]={0};
	wchar_t *p;
	if(wcslen(path)>=MAX_PATH) return 0;
	memset(m,0,sizeof(*m)); m->filetype=UINT32_MAX;
	r.file=_wfopen(path,L"rb"); if(!r.file) return 0;
	setvbuf(r.file,NULL,_IOFBF,65536);
	if(_fseeki64(r.file,0,SEEK_END) || (bytes=_ftelli64(r.file))<24 || _fseeki64(r.file,0,SEEK_SET)) goto done;
	m->bytes=(uint64_t)bytes;
	r.left=m->bytes<64*1024*1024?m->bytes:64*1024*1024;
	if(!take(&r,&magic,4) || magic!=0x46554747 || !take(&r,&version,4) || (version!=2 && version!=3) ||
	    !take(&r,&tensors,8) || !take(&r,&count,8) || count>100000 || tensors>1000000) goto done;
	for(i=0;i<count;i++) {
		if(!string(&r,key,sizeof(key)) || !take(&r,&type,4)) goto done;
		if(type==8 && (!strcmp(key,"general.name") || !strcmp(key,"general.basename") ||
		    !strcmp(key,"general.architecture") || !strcmp(key,"general.size_label") || !strcmp(key,"general.type"))) {
			if(!string(&r,value,sizeof(value))) goto done;
			if(!strcmp(key,"general.name")) {
				if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,m->name,256)) m->name[0]=0;
			} else if(!strcmp(key,"general.basename")) {
				if(strlen(value)<sizeof(basename)) strcpy(basename,value);
			} else if(!strcmp(key,"general.architecture")) {
				if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,m->architecture,80)) m->architecture[0]=0;
				if(!strcmp(value,"clip")) m->projector=1;
			} else if(!strcmp(key,"general.size_label")) {
				if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,m->size,40)) m->size[0]=0;
			} else if(!strcmp(value,"mmproj")) m->projector=1;
		} else if(type==4 && !strcmp(key,"general.file_type")) {
			if(!take(&r,&m->filetype,4)) goto done;
		} else if(!skip(&r,type)) goto done;
	}
	wcscpy(m->path,path);
	if(!m->name[0] && basename[0]) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,basename,-1,m->name,256);
	if(!m->name[0]) {
		const wchar_t *file=wcsrchr(path,L'\\'); file=file?file+1:path;
		wcsncpy(m->name,file,255); m->name[255]=0;
		p=wcsrchr(m->name,L'.'); if(p && !_wcsicmp(p,L".gguf")) *p=0;
		for(p=m->name;*p;p++) if(*p==L'_') *p=L' ';
	}
	ok=1;
done:
	fclose(r.file); return ok;
}

static void
walk(Library *lib, const wchar_t *folder, int recursive, unsigned depth, volatile LONG *cancel)
{
	wchar_t pattern[MAX_PATH],path[MAX_PATH];
	WIN32_FIND_DATAW data;
	HANDLE find;
	if(InterlockedCompareExchange(cancel,0,0)) return;
	if(depth>24 || lib->folders>=4096 || lib->count>=LibraryMax) { lib->limited=1; return; }
	if(wcslen(folder)>MAX_PATH-4) { lib->skipped++; return; }
	lib->folders++;
	swprintf(pattern,MAX_PATH,L"%ls\\*",folder);
	find=FindFirstFileW(pattern,&data);
	if(find==INVALID_HANDLE_VALUE) { lib->skipped++; return; }
	do {
		const wchar_t *ext;
		if(InterlockedCompareExchange(cancel,0,0)) break;
		if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
		if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) continue;
		if(wcslen(folder)+wcslen(data.cFileName)+2>=MAX_PATH) { lib->skipped++; continue; }
		swprintf(path,MAX_PATH,L"%ls\\%ls",folder,data.cFileName);
		if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
			if(recursive) walk(lib,path,recursive,depth+1,cancel);
			continue;
		}
		ext=wcsrchr(data.cFileName,L'.');
		if(!ext || _wcsicmp(ext,L".gguf")) continue;
		if(lib->count>=LibraryMax) { lib->limited=1; break; }
		if(model_read(path,&lib->models[lib->count])) lib->count++;
		else lib->skipped++;
	} while(FindNextFileW(find,&data));
	FindClose(find);
}

static int
compare(const void *a,const void *b)
{
	return _wcsicmp(((const ModelInfo *)a)->name,((const ModelInfo *)b)->name);
}

void
library_scan(Library *lib, const wchar_t *folder, int recursive, volatile LONG *cancel)
{
	memset(lib,0,sizeof(*lib));
	walk(lib,folder,recursive,0,cancel);
	qsort(lib->models,lib->count,sizeof(lib->models[0]),compare);
}

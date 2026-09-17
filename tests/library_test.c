#include "library_win.h"
#include "layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static void u32(FILE *f,uint32_t n) { assert(fwrite(&n,4,1,f)==1); }
static void u64(FILE *f,uint64_t n) { assert(fwrite(&n,8,1,f)==1); }
static void str(FILE *f,const char *s) { u64(f,strlen(s)); assert(fwrite(s,1,strlen(s),f)==strlen(s)); }
static void field(FILE *f,const char *key,const char *value) { str(f,key); u32(f,8); str(f,value); }
static FILE *header(const wchar_t *path,uint64_t fields) {
	FILE *f=_wfopen(path,L"wb"); assert(f);
	u32(f,0x46554747); u32(f,3); u64(f,0); u64(f,fields); return f;
}

int main(int argc,char **argv)
{
	wchar_t temp[MAX_PATH],root[MAX_PATH],sub[MAX_PATH],a[MAX_PATH],b[MAX_PATH],bad[MAX_PATH];
	Library *lib=calloc(1,sizeof(*lib)); ModelInfo m;
	volatile LONG cancel=0;
	FILE *f;
	int width,height;
	assert(lib);
	if(argc==2) {
		assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,root,MAX_PATH));
		library_scan(lib,root,1,&cancel);
		printf("Files: %u; skipped: %u; limited: %d\n",lib->count,lib->skipped,lib->limited);
		for(unsigned i=0;i<lib->count;i++) wprintf(L"%ls | %ls | %ls\n",lib->models[i].name,lib->models[i].architecture,lib->models[i].path);
		free(lib); return 0;
	}
	assert(GetTempPathW(MAX_PATH,temp));
	swprintf(root,MAX_PATH,L"%lsLTI-library-%lu-%llu",temp,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
	swprintf(sub,MAX_PATH,L"%ls\\nested",root);
	assert(CreateDirectoryW(root,NULL)); assert(CreateDirectoryW(sub,NULL));
	swprintf(a,MAX_PATH,L"%ls\\fallback_name.gguf",root);
	swprintf(b,MAX_PATH,L"%ls\\vision.gguf",sub);
	swprintf(bad,MAX_PATH,L"%ls\\broken.gguf",root);
	f=header(a,5); field(f,"general.name","Local \xCE\xA9 model"); field(f,"general.architecture","qwen35");
	field(f,"general.size_label","2B"); str(f,"general.file_type"); u32(f,4); u32(f,18);
	str(f,"tokenizer.tokens"); u32(f,9); u32(f,8); u64(f,2); str(f,"one"); str(f,"two"); fclose(f);
	assert(model_read(a,&m)); assert(!wcscmp(m.name,L"Local \x03a9 model")); assert(m.filetype==18); assert(!m.projector);
	f=header(b,1); field(f,"general.architecture","clip"); fclose(f);
	assert(model_read(b,&m) && m.projector);
	f=header(bad,1); u64(f,UINT64_MAX); fclose(f); assert(!model_read(bad,&m));
	library_scan(lib,root,0,&cancel); assert(lib->count==1 && lib->skipped==1);
	library_scan(lib,root,1,&cancel); assert(lib->count==2 && lib->skipped==1);
	InterlockedExchange(&cancel,1); library_scan(lib,root,1,&cancel); assert(!lib->count);
	f=header(a,0); fclose(f); assert(model_read(a,&m)); assert(!wcscmp(m.name,L"fallback name"));
	f=header(a,1); str(f,"bad"); u32(f,9); u32(f,8); u64(f,UINT64_MAX); fclose(f); assert(!model_read(a,&m));
	f=header(a,1); str(f,"bad"); u32(f,99); fclose(f); assert(!model_read(a,&m));
	f=header(a,1); str(f,"general.name"); u32(f,8); u64(f,99); fclose(f); assert(!model_read(a,&m));
	for(width=984;width<=2560;width+=37) for(height=621;height<=1600;height+=29) {
		PaneLayout p=pane_layout(width,height);
		assert(p.center>=400 && p.rightx+p.right<width);
		assert(p.left+5+p.center<p.rightx && p.composer>100);
		assert(p.bottom-294>0 && p.bottom+99<=height-24);
	}
	assert(DeleteFileW(a)); assert(DeleteFileW(b)); assert(DeleteFileW(bad));
	assert(RemoveDirectoryW(sub)); assert(RemoveDirectoryW(root));
	free(lib); puts("Library and layout tests passed."); return 0;
}

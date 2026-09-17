#include "engine_win.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	EngineProcess p={0};
	wchar_t exe[MAX_PATH], temp[MAX_PATH], model[MAX_PATH], error[256];
	HANDLE file, process;
	DWORD n;
	if(argc>1 && strcmp(argv[1],"--model")==0) {
		/* Test child stands in for a long-running engine. */
		Sleep(INFINITE); return 0;
	}
	if(argc==3 && strcmp(argv[1],"probe")==0) {
		printf("%d\n",engine_probe((unsigned short)atoi(argv[2]))); return 0;
	}
	GetModuleFileNameW(NULL,exe,MAX_PATH);
	GetTempPathW(MAX_PATH,temp);
	swprintf(model,MAX_PATH,L"%lsLTS test model %lu.gguf",temp,(unsigned long)GetCurrentProcessId());
	file=CreateFileW(model,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,NULL);
	assert(file!=INVALID_HANDLE_VALUE); CloseHandle(file);
	if(argc==3 && strcmp(argv[1],"occupied")==0) {
		assert(!engine_start(&p,exe,model,(unsigned short)atoi(argv[2]),error,256));
		assert(p.process==NULL && p.job==NULL);
		assert(wcsstr(error,L"already in use")!=NULL);
	} else {
		assert(!engine_start(&p,L"missing.exe",model,18089,error,256));
		assert(engine_start(&p,exe,model,18089,error,256));
		assert(WaitForSingleObject(p.process,0)==WAIT_TIMEOUT);
		assert(!engine_start(&p,exe,model,18089,error,256));
		assert(p.process!=NULL);
		assert(DuplicateHandle(GetCurrentProcess(),p.process,GetCurrentProcess(),&process,0,FALSE,DUPLICATE_SAME_ACCESS));
		engine_stop(&p);
		assert(p.process==NULL && p.job==NULL);
		assert(WaitForSingleObject(process,5000)==WAIT_OBJECT_0);
		assert(GetExitCodeProcess(process,&n) && n!=STILL_ACTIVE);
		CloseHandle(process);
		engine_stop(&p);
	}
	assert(DeleteFileW(model));
	puts("Engine lifecycle checks passed.");
	return 0;
}

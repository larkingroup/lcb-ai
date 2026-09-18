#include "app_paths.h"
#include <stdio.h>
#include <wchar.h>

int
app_data_root(const wchar_t *base, wchar_t root[MAX_PATH])
{
    static const wchar_t *folders[] = {L"lcb-ai", L"lts-ai", L"lti-ai"};
    size_t i;
    DWORD error;
    root[0] = 0;
    if(!base || !base[0] || wcslen(base) > MAX_PATH-80) return 0;
    for(i=0; i<sizeof(folders)/sizeof(folders[0]); i++) {
        swprintf(root,MAX_PATH,L"%ls\\%ls",base,folders[i]);
        if(GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) return 1;
        error=GetLastError();
        if(error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            root[0]=0; return 0;
        }
    }
    swprintf(root,MAX_PATH,L"%ls\\lcb-ai",base);
    return 1;
}

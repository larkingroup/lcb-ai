#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "model_settings_win.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    wchar_t temp[MAX_PATH],config[MAX_PATH];
    Generation a,b,loaded;
    assert(GetTempPathW(MAX_PATH,temp));
    assert(GetTempFileNameW(temp,L"lcp",0,config));
    assert(WritePrivateProfileStringW(L"generation",L"max_tokens",L"256",config));
    assert(WritePrivateProfileStringW(L"generation",L"temperature100",L"20",config));
    model_settings_load(config,L"C:\\Models\\first.gguf",&a);
    assert(a.max_tokens==256 && a.temperature==0.2 && a.thinking==ThinkingAuto);
    a.context_tokens=8192; a.thinking=ThinkingOn; a.dry_multiplier=0.8; a.repeat_penalty=1.1;
    assert(model_settings_save(config,L"C:\\Models\\first.gguf",&a));
    model_settings_load(config,L"C:\\Models\\second.gguf",&b);
    assert(b.context_tokens==4096 && b.thinking==ThinkingAuto && b.dry_multiplier==0);
    b.max_tokens=512; b.thinking=ThinkingOff;
    assert(model_settings_save(config,L"C:\\Models\\second.gguf",&b));
    model_settings_load(config,L"c:/models/FIRST.gguf",&loaded);
    assert(loaded.context_tokens==8192 && loaded.max_tokens==256 && loaded.thinking==ThinkingOn);
    assert(loaded.repeat_penalty==1.1 && loaded.dry_multiplier==0.8);
    model_settings_load(config,L"C:\\Models\\second.gguf",&loaded);
    assert(loaded.context_tokens==4096 && loaded.max_tokens==512 && loaded.thinking==ThinkingOff);
    loaded.context_tokens=0;
    assert(!model_settings_save(config,L"C:\\Models\\second.gguf",&loaded));
    model_settings_load(config,L"C:\\Models\\second.gguf",&loaded);
    assert(loaded.context_tokens==4096 && loaded.max_tokens==512);
    assert(!model_settings_save(config,L"",&loaded));
    assert(GetPrivateProfileIntW(L"generation",L"max_tokens",0,config)==256);
    assert(DeleteFileW(config));
    puts("Model settings passed: legacy defaults, separate models, normalized paths, persistence, invalid values.");
    return 0;
}

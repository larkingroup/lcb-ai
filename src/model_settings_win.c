#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <wctype.h>
#include "model_settings_win.h"
#include "model_defaults.h"

static const wchar_t *keys[GenCount]={L"max_tokens",L"temperature",L"top_p",L"context_tokens",
    L"thinking",L"repeat_penalty",L"dry_multiplier",L"top_k",L"min_p",L"presence_penalty"};

/* Key by the normalized full file path, not llama-server's shared "local" alias. */
static int section_name(const wchar_t *model, wchar_t section[80])
{
    wchar_t path[MAX_PATH];
    unsigned char digest[32];
    size_t i;
    DWORD n;
    if(!model[0]) return 0;
    n=GetFullPathNameW(model,MAX_PATH,path,NULL);
    if(!n || n>=MAX_PATH) return 0;
    for(i=0;i<n;i++) path[i]=path[i]==L'/'?L'\\':towlower(path[i]);
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)path,(ULONG)(n*sizeof(wchar_t)),digest,sizeof(digest))) return 0;
    wcscpy(section,L"model-");
    for(i=0;i<32;i++) swprintf(section+6+i*2,3,L"%02x",digest[i]);
    return 1;
}

static double read_number(const wchar_t *config, const wchar_t *section,
    const wchar_t *key, double fallback, double low, double high, int integer)
{
    wchar_t text[64],*end;
    double n;
    GetPrivateProfileStringW(section,key,L"",text,64,config);
    n=wcstod(text,&end);
    if(end==text || *end || !(n>=low && n<=high) || (integer && n!=(int)n)) return fallback;
    return n;
}

void model_settings_load(const wchar_t *config, const wchar_t *model, Generation *g)
{
    wchar_t section[80];
    ModelInfo info;
    int field, has_section=section_name(model,section);
    int thinking=has_section?(int)read_number(config,section,L"thinking",-1,ThinkingAuto,ThinkingOn,1):-1;
    model_defaults(model_read(model,&info)?&info:NULL,thinking,g);
    /* Older saved values remain user choices, including old full sections. */
    g->max_tokens=(int)read_number(config,L"generation",L"max_tokens",g->max_tokens,1,16384,1);
    g->temperature=read_number(config,L"generation",L"temperature100",g->temperature*100,0,200,1)/100;
    g->top_p=read_number(config,L"generation",L"top_p100",g->top_p*100,1,100,1)/100;
    if(!has_section) return;
    for(field=0;field<GenCount;field++) {
        double value=read_number(config,section,keys[field],generation_value(g,field),-2,1048576,0);
        generation_set(g,field,value);
    }
}

int model_settings_save(const wchar_t *config, const wchar_t *model, const Generation *g)
{
    wchar_t section[80], data[768];
    int n;
    if(!generation_valid(g) || !section_name(model,section)) return 0;
    n=swprintf(data,768,L"max_tokens=%d|temperature=%.17g|top_p=%.17g|context_tokens=%d|thinking=%d|repeat_penalty=%.17g|dry_multiplier=%.17g|top_k=%d|min_p=%.17g|presence_penalty=%.17g|",
        g->max_tokens,g->temperature,g->top_p,g->context_tokens,g->thinking,g->repeat_penalty,g->dry_multiplier,
        g->top_k,g->min_p,g->presence_penalty);
    if(n<0 || n>=767) return 0;
    for(int i=0;i<n;i++) if(data[i]==L'|') data[i]=0;
    return WritePrivateProfileSectionW(section,data,config)!=0;
}

int model_settings_save_field(const wchar_t *config, const wchar_t *model, const Generation *g, int field)
{
    wchar_t section[80], value[64];
    if(field<0 || field>=GenCount || !generation_valid(g) || !section_name(model,section)) return 0;
    swprintf(value,64,L"%.17g",generation_value(g,field));
    return WritePrivateProfileStringW(section,keys[field],value,config)!=0;
}

#include "model_defaults.h"
#include "model_settings_win.h"
#include "context_win.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void u32(FILE *f,uint32_t v) { assert(fwrite(&v,4,1,f)==1); }
static void u64(FILE *f,uint64_t v) { assert(fwrite(&v,8,1,f)==1); }
static void str(FILE *f,const char *s) { u64(f,strlen(s)); assert(fwrite(s,1,strlen(s),f)==strlen(s)); }
static void textfield(FILE *f,const char *key,const char *value) { str(f,key); u32(f,8); str(f,value); }
static void fixture(const wchar_t *file, double top_p)
{
    FILE *f=_wfopen(file,L"wb"); assert(f);
    u32(f,0x46554747); u32(f,3); u64(f,0); u64(f,7);
    textfield(f,"general.name","Qwen3-8B"); textfield(f,"general.architecture","qwen3");
    textfield(f,"tokenizer.chat_template","{% if enable_thinking %}thinking{% endif %}");
    str(f,"general.sampling.top_p"); u32(f,12); assert(fwrite(&top_p,8,1,f)==1);
    str(f,"general.sampling.top_k"); u32(f,4); u32(f,17);
    str(f,"general.sampling.temp"); u32(f,12); top_p=NAN; assert(fwrite(&top_p,8,1,f)==1);
    str(f,"general.sampling.penalty_repeat"); u32(f,6); { float repeat=1.125f; assert(fwrite(&repeat,4,1,f)==1); }
    assert(!fclose(f));
}

static ModelInfo named(const wchar_t *name,const wchar_t *arch)
{
    ModelInfo m={0};
    wcscpy(m.identity,name); wcscpy(m.architecture,arch);
    m.thinking_switch=1; m.sampling=generation_defaults(); return m;
}

int main(int argc,char **argv)
{
    ModelInfo m;
    Generation g;
    wchar_t temp[MAX_PATH],config[MAX_PATH],file[MAX_PATH];
    if(argc==2 || argc==3) {
        LARGE_INTEGER begin,end,frequency;
        assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,file,MAX_PATH));
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
        assert(model_read(file,&m)); model_defaults(&m,-1,&g); QueryPerformanceCounter(&end);
        wprintf(L"Model: %ls\nArchitecture: %ls\n",m.identity,m.architecture);
        printf("Defaults: temp %.2f, top_p %.2f, top_k %d, min_p %.2f, presence %.2f, thinking %d, response %d, context %d\nHeader/defaults: %.2f ms\n",
            g.temperature,g.top_p,g.top_k,g.min_p,g.presence_penalty,g.thinking,g.max_tokens,g.context_tokens,
            (double)(end.QuadPart-begin.QuadPart)*1000/frequency.QuadPart);
        if(argc==3) {
            Conversation conversation={0}; Module module={"Check","Answer briefly."};
            ContextBudget budget; ReplyReport report;
            HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
            char *request=NULL,*answer=NULL,error[256];
            unsigned short port=(unsigned short)atoi(argv[2]);
            int result;
            assert(cancel && port);
            assert(local_prepare(port,file,&conversation,&module,"Say hello in one short sentence.",
                &g,cancel,&request,&budget,error,sizeof(error)));
            result=local_stream_report(port,request,cancel,NULL,NULL,&answer,&report,error,sizeof(error));
            printf("Live result: %d; tokens: %d; finish: %d; reasoning: %d; answer: %s; error: %s\n",
                result,report.tokens,report.finish,report.saw_reasoning,answer?answer:"",error);
            assert(result==1 && answer && report.finish==FinishStop && !report.saw_reasoning);
            free(request); free(answer); CloseHandle(cancel);
        }
        return 0;
    }
    m=named(L"Qwen3.5 2B Uncensored HauhauCS Aggressive",L"qwen35");
    model_defaults(&m,-1,&g);
    assert(g.thinking==ThinkingOff && g.temperature==0.7 && g.top_p==0.8 && g.top_k==20 && g.min_p==0);
    m.sampling_fields=1u<<GenTemperature; m.sampling.temperature=1.2;
    model_defaults(&m,-1,&g); assert(g.temperature==0.7); /* Reviewed exact exception. */
    m=named(L"Qwen3.5-2B",L"qwen35"); model_defaults(&m,-1,&g);
    assert(g.temperature==1 && g.presence_penalty==2 && g.context_tokens==4096);
    m=named(L"Qwen3-8B",L"qwen3"); model_defaults(&m,ThinkingOn,&g);
    assert(g.temperature==0.6 && g.top_p==0.95 && g.max_tokens==2048);
    m.thinking_switch=0; model_defaults(&m,-1,&g); assert(g.thinking==ThinkingAuto);
    m=named(L"Qwen3-8B-Base",L"qwen3"); model_defaults(&m,-1,&g); assert(g.top_k==40 && g.thinking==ThinkingAuto);
    m=named(L"Qwen3-8B-Thinking-2507",L"qwen3"); model_defaults(&m,-1,&g); assert(g.top_k==40);
    m=named(L"Qwen3-80B",L"qwen3"); model_defaults(&m,-1,&g); assert(g.top_k==40);
    m=named(L"Qwen3-8B",L"llama"); model_defaults(&m,-1,&g); assert(g.top_k==40);
    m=named(L"Unrelated model",L"qwen3"); wcscpy(m.path,L"C:\\Models\\Qwen3-8B-Q4_K_M.gguf");
    model_defaults(&m,-1,&g); assert(g.top_k==40); /* Filename cannot contradict metadata. */
    m.identity[0]=0; model_defaults(&m,-1,&g); assert(g.top_k==20);
    m=named(L"Custom chat",L"qwen3"); wcscpy(m.base_model,L"https://huggingface.co/Qwen/Qwen3-8B");
    model_defaults(&m,-1,&g); assert(g.top_k==20);
    m.base_count=2; model_defaults(&m,-1,&g); assert(g.top_k==40);
    m.base_count=1; wcscpy(m.identity,L"DeepSeek-R1-Distill-Qwen3-8B");
    model_defaults(&m,-1,&g); assert(g.top_k==40 && g.thinking==ThinkingAuto);
    m=named(L"SmolLM2-135M-Instruct-Q8_0.gguf",L"llama"); model_defaults(&m,-1,&g);
    assert(g.temperature==0.2 && g.top_p==0.9 && g.max_tokens==256 && g.top_k==50);
    m=named(L"Qwen2.5-Coder-7B-Instruct",L"qwen2"); model_defaults(&m,-1,&g); assert(g.repeat_penalty==1.1);
    m=named(L"Qwen2.5-Coder-32B-Instruct",L"qwen2"); model_defaults(&m,-1,&g); assert(g.repeat_penalty==1.05);
    m=named(L"Meta-Llama-3.1-8B-Instruct-Q6_K.gguf",L"llama"); model_defaults(&m,-1,&g);
    assert(g.temperature==0.6 && g.top_p==0.9 && g.top_k==0);
    m=named(L"gemma-3-4b-it",L"gemma3"); model_defaults(&m,-1,&g);
    assert(g.temperature==1 && g.top_k==64 && g.top_p==0.95);
    m=named(L"Unknown",L"llama"); m.sampling_fields=(1u<<GenTemperature)|(1u<<GenTopK);
    m.sampling.temperature=0.42; m.sampling.top_k=12; model_defaults(&m,-1,&g);
    assert(g.temperature==0.42 && g.top_k==12);
    assert(!generation_set(&g,GenTopK,1.5)); assert(!generation_set(&g,GenMinP,NAN));
    assert(!generation_set(&g,GenTemperature,INFINITY)); assert(generation_set(&g,GenPresence,-1));
    assert(GetTempPathW(MAX_PATH,temp)); assert(GetTempFileNameW(temp,L"lcd",0,config));
    assert(GetTempFileNameW(temp,L"lcd",0,file)); fixture(file,0.73);
    assert(model_read(file,&m)); assert(m.thinking_switch && !(m.sampling_fields&(1u<<GenTemperature)));
    model_settings_load(config,file,&g);
    assert(g.temperature==0.7 && g.top_p==0.73 && g.top_k==17 && g.repeat_penalty==1.125 && g.thinking==ThinkingOff);
    g.temperature=0.23; assert(model_settings_save_field(config,file,&g,GenTemperature));
    fixture(file,0.67); model_settings_load(config,file,&g);
    assert(g.temperature==0.23 && g.top_p==0.67); /* An edit doesn't freeze every default. */
    g.thinking=ThinkingOn; assert(model_settings_save_field(config,file,&g,GenThinking));
    model_settings_load(config,file,&g); assert(g.temperature==0.23 && g.thinking==ThinkingOn && g.max_tokens==2048);
    assert(model_settings_save(config,file,&g)); /* Previous builds saved full sections. */
    fixture(file,0.71); model_settings_load(config,file,&g); assert(g.top_p==0.67 && g.thinking==ThinkingOn);
    assert(DeleteFileW(file)); assert(DeleteFileW(config));
    puts("Defaults passed: identities, variants, embedded settings, capabilities, per-field overrides, legacy settings, invalid values.");
    return 0;
}

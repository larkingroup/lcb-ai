#include "model_defaults.h"
#include <wchar.h>
#include <string.h>

typedef struct Profile {
    const wchar_t *name, *architecture;
    double temperature, top_p, repeat, presence;
    int top_k, response, thinking, exact;
} Profile;

/* Reviewed 2026-09-19. Sampling comes from the linked author model cards or
 * generation_config.json files. Response lengths are local chat starting
 * limits, not author benchmark budgets. Context stays independent.
 * Aliases are complete model/version/size identifiers, never bare families. */
#define Q2(name,arch,repeat) {L##name,L##arch,0.7,0.8,repeat,0,20,1024,0,0}
#define Q3(name,arch) {L##name,L##arch,0.7,0.8,1,0,20,1024,1,0}
#define Q35(name,arch,t,p,penalty) {L##name,L##arch,t,p,1,penalty,20,1024,2,0}
#define LLAMA(name) {L##name,L"llama",0.6,0.9,1,0,0,1024,0,0}
#define GEMMA(name,arch) {L##name,L##arch,1.0,0.95,1,0,64,1024,0,0}
static const Profile profiles[]={
    /* https://huggingface.co/Qwen/<model>/blob/main/generation_config.json */
    Q2("Qwen2-0.5B-Instruct","qwen2",1.1),
    Q2("Qwen2-1.5B-Instruct","qwen2",1.1),
    Q2("Qwen2-7B-Instruct","qwen2",1.05),
    Q2("Qwen2-57B-A14B-Instruct","qwen2moe",1.05),
    Q2("Qwen2-72B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-0.5B-Instruct","qwen2",1.1),
    Q2("Qwen2.5-1.5B-Instruct","qwen2",1.1),
    Q2("Qwen2.5-3B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-7B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-14B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-32B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-72B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-Coder-0.5B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-Coder-1.5B-Instruct","qwen2",1.1),
    Q2("Qwen2.5-Coder-3B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-Coder-7B-Instruct","qwen2",1.1),
    Q2("Qwen2.5-Coder-14B-Instruct","qwen2",1.05),
    Q2("Qwen2.5-Coder-32B-Instruct","qwen2",1.05),
    /* https://huggingface.co/Qwen/Qwen3-8B#best-practices */
    Q3("Qwen3-0.6B","qwen3"), Q3("Qwen3-1.7B","qwen3"),
    Q3("Qwen3-4B","qwen3"), Q3("Qwen3-8B","qwen3"),
    Q3("Qwen3-14B","qwen3"), Q3("Qwen3-32B","qwen3"),
    Q3("Qwen3-30B-A3B","qwen3moe"), Q3("Qwen3-235B-A22B","qwen3moe"),
    Q2("Qwen3-4B-Instruct-2507","qwen3",1),
    Q2("Qwen3-30B-A3B-Instruct-2507","qwen3moe",1),
    Q2("Qwen3-235B-A22B-Instruct-2507","qwen3moe",1),
    /* https://huggingface.co/Qwen/Qwen3-Coder-30B-A3B-Instruct#best-practices */
    Q2("Qwen3-Coder-30B-A3B-Instruct","qwen3moe",1.05),
    Q2("Qwen3-Coder-480B-A35B-Instruct","qwen3moe",1.05),
    /* https://huggingface.co/Qwen/Qwen3.5-2B#best-practices
     * https://huggingface.co/Qwen/Qwen3.5-4B#best-practices */
    Q35("Qwen3.5-0.8B","qwen35",1.0,1.0,2.0),
    Q35("Qwen3.5-2B","qwen35",1.0,1.0,2.0),
    Q35("Qwen3.5-4B","qwen35",0.7,0.8,1.5),
    Q35("Qwen3.5-9B","qwen35",0.7,0.8,1.5),
    Q35("Qwen3.5-27B","qwen35",0.7,0.8,1.5),
    Q35("Qwen3.5-35B-A3B","qwen35moe",0.7,0.8,1.5),
    Q35("Qwen3.5-122B-A10B","qwen35moe",0.7,0.8,1.5),
    Q35("Qwen3.5-397B-A17B","qwen35moe",0.7,0.8,1.5),
    /* https://huggingface.co/HauhauCS/Qwen3.5-2B-Uncensored-HauhauCS-Aggressive */
    {L"Qwen3.5-2B-Uncensored-HauhauCS-Aggressive",L"qwen35",0.7,0.8,1,0,20,1024,1,1},
    /* https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct#how-to-use */
    {L"SmolLM2-135M-Instruct",L"llama",0.2,0.9,1,0,50,256,0,0},
    {L"SmolLM2-360M-Instruct",L"llama",0.2,0.9,1,0,50,256,0,0},
    {L"SmolLM2-1.7B-Instruct",L"llama",0.2,0.9,1,0,50,1024,0,0},
    /* Meta reference sampler uses temperature + nucleus, with no top-k cutoff:
     * https://github.com/meta-llama/llama-models/blob/main/models/llama3/generation.py */
    LLAMA("Llama-3-8B-Instruct"), LLAMA("Llama-3-70B-Instruct"),
    LLAMA("Llama-3.1-8B-Instruct"), LLAMA("Llama-3.1-70B-Instruct"),
    LLAMA("Llama-3.1-405B-Instruct"), LLAMA("Llama-3.2-1B-Instruct"),
    LLAMA("Llama-3.2-3B-Instruct"), LLAMA("Llama-3.3-70B-Instruct"),
    /* Google reference defaults for Gemma 1/2/3:
     * https://github.com/google/gemma_pytorch/blob/main/gemma/model.py */
    GEMMA("gemma-2b-it","gemma"), GEMMA("gemma-7b-it","gemma"),
    GEMMA("gemma-2-2b-it","gemma2"), GEMMA("gemma-2-9b-it","gemma2"),
    GEMMA("gemma-2-27b-it","gemma2"), GEMMA("gemma-3-1b-it","gemma3"),
    GEMMA("gemma-3-4b-it","gemma3"), GEMMA("gemma-3-12b-it","gemma3"),
    GEMMA("gemma-3-27b-it","gemma3")
};
#undef Q2
#undef Q3
#undef Q35
#undef LLAMA
#undef GEMMA

static void normalize(const wchar_t *s, wchar_t *out, size_t capacity)
{
    size_t n=0;
    for(;*s && n+1<capacity;s++) {
        wchar_t c=*s;
        if(c>=L'A' && c<=L'Z') c+=L'a'-L'A';
        if((c>=L'a' && c<=L'z') || (c>=L'0' && c<=L'9')) out[n++]=c;
    }
    out[n]=0;
}

static const Profile *lookup(const wchar_t *identity, const wchar_t *arch)
{
    wchar_t name[512],key[256];
    const wchar_t *last=wcsrchr(identity,L'/');
    size_t i;
    normalize(last?last+1:identity,name,512);
    if(!wcsncmp(name,L"metallama",9)) memmove(name,name+4,(wcslen(name+4)+1)*sizeof(*name));
    for(i=0;i<sizeof(profiles)/sizeof(profiles[0]);i++) {
        const Profile *p=&profiles[i];
        const wchar_t *suffix;
        size_t n;
        if(arch[0] && _wcsicmp(arch,p->architecture)) continue;
        normalize(p->name,key,256); n=wcslen(key);
        if(wcsncmp(name,key,n)) continue;
        suffix=name+n;
        /* Only quantization/file suffixes are ignored. New versions, thinking
         * variants, distillations and unrelated fine-tunes don't prefix-match. */
        if(!*suffix || !wcscmp(suffix,L"gguf") ||
            (suffix[0]==L'q' && suffix[1]>=L'0' && suffix[1]<=L'9') ||
            (suffix[0]==L'i' && suffix[1]==L'q' && suffix[2]>=L'0' && suffix[2]<=L'9') ||
            !wcsncmp(suffix,L"bf16",4) || !wcsncmp(suffix,L"f16",3) || !wcsncmp(suffix,L"f32",3)) return p;
    }
    return NULL;
}

static void apply(const Profile *p, int thinking, Generation *g)
{
    g->temperature=p->temperature; g->top_p=p->top_p;
    g->repeat_penalty=p->repeat; g->presence_penalty=p->presence;
    g->top_k=p->top_k; g->min_p=0; g->max_tokens=p->response;
    if(p->thinking && thinking==ThinkingOn) {
        g->temperature=p->thinking==2?1.0:0.6; g->top_p=0.95;
        g->presence_penalty=p->thinking==2?1.5:0;
        g->max_tokens=2048;
    }
}

void model_defaults(const ModelInfo *m, int thinking, Generation *g)
{
    const Profile *p=NULL;
    wchar_t identity[512];
    int field;
    *g=generation_defaults();
    if(!m || m->projector) return;
    normalize(m->identity,identity,512);
    if(m->identity[0]) p=lookup(m->identity,m->architecture);
    if(!p && m->base_count<=1 && m->base_model[0] &&
        !wcsstr(identity,L"thinking") && !wcsstr(identity,L"reason") &&
        !wcsstr(identity,L"distill") && !wcsstr(identity,L"r1")) p=lookup(m->base_model,m->architecture);
    if(!p && !m->identity[0] && !m->base_model[0]) {
        const wchar_t *name=wcsrchr(m->path,L'\\');
        if(!name) name=wcsrchr(m->path,L'/');
        p=lookup(name?name+1:m->path,m->architecture);
    }
    if(p) apply(p,thinking,g);
    for(field=0;field<GenCount;field++) if(m->sampling_fields&(1u<<field))
        generation_set(g,field,generation_value(&m->sampling,field));
    if(p && p->exact) apply(p,thinking,g);
    if(p && p->thinking && m->thinking_switch) g->thinking=ThinkingOff;
}

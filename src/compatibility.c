#include "compatibility.h"
#include <wctype.h>

static int contains(const wchar_t *text, const wchar_t *part)
{
    const wchar_t *a, *b;
    for(; *text; text++) {
        for(a=text,b=part; *a && *b && towlower(*a)==towlower(*b); a++,b++) {}
        if(!*b) return 1;
    }
    return 0;
}

const wchar_t *runtime_setup_url(void)
{
    return L"https://github.com/PrismML-Eng/Bonsai-demo";
}

const wchar_t *runtime_requirement_text(int requirement)
{
    switch(requirement) {
    case RuntimePTQ: return L"This GGUF uses PrismML PTQ1_0 weights. It requires an engine with PrismML's ternary kernels and Hadamard transforms.";
    case RuntimePQ: return L"This GGUF uses PrismML PQ2_0 weights. It requires an engine with PrismML's ternary kernels and Hadamard transforms.";
    case RuntimeHadamard: return L"This GGUF declares PrismML Hadamard transforms. An engine without those transforms may load it but produce incorrect answers.";
    default: return L"";
    }
}

const wchar_t *engine_failure_advice(const wchar_t *log)
{
    if(contains(log,L"out of memory") || contains(log,L"not enough memory") ||
       contains(log,L"failed to allocate") || contains(log,L"cudaMalloc failed"))
        return L"The engine reported a memory allocation failure. Close other loaded models, lower Context and reload, or choose a smaller model.";
    if(contains(log,L"error: invalid argument") || contains(log,L"unrecognized argument") ||
       contains(log,L"unknown argument") || contains(log,L"unrecognized option"))
        return L"The selected engine rejected a launch option. Use a compatible llama-server build; inspect the log for the rejected option.";
    if(contains(log,L"invalid ggml type") || contains(log,L"invalid type 143") || contains(log,L"invalid type 142") || contains(log,L"unsupported tensor type") || contains(log,L"unknown tensor type"))
        return L"The engine cannot read a tensor format in this model. Select a build that supports the model's quantization. For PTQ1_0/PQ2_0, follow PrismML's engine setup guide. Renaming the GGUF or adding a projector will not fix this.";
    if(contains(log,L"unknown model architecture") || contains(log,L"unsupported model architecture"))
        return L"The engine does not support this model architecture. Select a compatible engine release recommended by the model publisher.";
    if(contains(log,L"unexpected end of file") || contains(log,L"unexpected eof") || contains(log,L"file too short"))
        return L"The engine reported an incomplete file. Verify its size and publisher checksum; complete any missing model shards or interrupted download before retrying.";
    if(contains(log,L"permission denied") || contains(log,L"access is denied"))
        return L"The engine could not access a required file. Check file permissions and whether the network share is mounted.";
    return L"The engine stopped. Its summary alone does not identify the cause. Inspect the log below for a format, memory, missing-file, or driver error before changing or downloading the model.";
}

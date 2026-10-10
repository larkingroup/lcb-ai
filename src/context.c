#include "lcb.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int candidate(const Conversation *c, size_t first, const Module *module,
    const char *prompt, const Generation *g, int effort, PromptCounter counter,
    void *context, char **wire, int *count, char *error, size_t capacity)
{
    Conversation view=conversation_suffix(c,first);
    *wire=NULL;
    /* Avoid allocating an unbounded copy of saved history. */
    if(view.bytes>(LcbMaxWire-200000)/6 || view.count>(LcbMaxWire-200000)/64 ||
        view.bytes*6+view.count*64+200000>LcbMaxWire) return CountTooLarge;
    *wire=conversation_generate_capable(&view,module,prompt,g,effort);
    if(!*wire) { snprintf(error,capacity,"Cannot build the conversation request."); return CountFailed; }
    return counter(*wire,context,count,error,capacity);
}

int conversation_fit(const Conversation *c, const Module *module, const char *prompt,
    const Generation *g, int context_tokens, int effort, PromptCounter counter,
    void *counter_context, char **request, FitBudget *budget, char *error, size_t capacity)
{
    size_t low=0,high,mid;
    int count=0,status,available,ok=0;
    char *wire=NULL;
    const char *problem;
    *request=NULL; memset(budget,0,sizeof(*budget));
    if(!capacity) return 0;
    error[0]=0;
    if(!c || !module || !prompt || !counter || !generation_valid(g) || c->count%2 ||
        !prompt[0] || strlen(prompt)>LcbMaxPrompt || context_tokens<1) {
        snprintf(error,capacity,"Invalid context, generation settings or current message."); return 0;
    }
    problem=reasoning_settings_error(g,effort);
    if(problem) { snprintf(error,capacity,"%s",problem); return 0; }
    available=context_tokens-g->max_tokens-32;
    if(available<1) { snprintf(error,capacity,"Response limit leaves no prompt room in the loaded context."); return 0; }
    /* The common case needs one exact count, independent of history length. */
    status=candidate(c,0,module,prompt,g,effort,counter,counter_context,&wire,&count,error,capacity);
    if(status==CountFailed) goto done;
    if(status==CountOk && count<=available) {
        *request=wire; wire=NULL; budget->prompt_tokens=count; ok=1; goto done;
    }
    free(wire); wire=NULL;
    status=candidate(c,c->count,module,prompt,g,effort,counter,counter_context,&wire,&count,error,capacity);
    if(status!=CountOk) goto done;
    if(count>available) {
        snprintf(error,capacity,"Instructions and current message need %d tokens; only %d available.",count,available); goto done;
    }
    *request=wire; wire=NULL; budget->prompt_tokens=count; budget->omitted_messages=c->count;
    /* Full history failed. Search whole exchanges, retaining the largest
       verified suffix instead of discarding history after an estimate limit. */
    high=c->count/2;
    if(high) high--;
    while(low<high) {
        mid=low+(high-low+1)/2;
        status=candidate(c,c->count-mid*2,module,prompt,g,effort,counter,counter_context,&wire,&count,error,capacity);
        if(status==CountFailed) goto done;
        if(status==CountOk && count<=available) {
            low=mid; free(*request); *request=wire; wire=NULL;
            budget->prompt_tokens=count; budget->omitted_messages=c->count-mid*2;
        } else high=mid-1;
        free(wire); wire=NULL;
    }
    ok=1;
done:
    free(wire);
    if(ok) error[0]=0;
    else { free(*request); *request=NULL; }
    return ok;
}

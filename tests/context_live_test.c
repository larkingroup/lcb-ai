/* Optional integration check; uses an already-running local model. Never downloads. */
#include "context_win.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(unsigned short port,int thinking)
{
    Conversation c={0};
    Module module={"Test","Answer briefly."};
    Generation g=generation_defaults();
    ContextBudget budget;
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    char *request=NULL,*wire=NULL,*body=NULL,error[256];
    cJSON *json=NULL,*response=NULL,*usage,*tokens;
    size_t i;
    int ok=0;
    if(!cancel) return 2;
    g.max_tokens=16; g.thinking=thinking;
    for(i=0;i<100;i++) {
        if(!conversation_add(&c,"user","Remember this: the notebook is blue. Unicode: caf\xc3\xa9 \xe2\x98\xba.\nThe desk is wooden. The window faces north.") ||
            !conversation_add(&c,"assistant","The notebook is blue; the desk is wooden; the window faces north.")) goto done;
    }
    if(!local_prepare(port,NULL,&c,&module,"What color is the notebook?",&g,cancel,&request,&budget,error,sizeof(error))) {
        fprintf(stderr,"prepare: %s\n",error); goto done;
    }
    json=cJSON_Parse(request);
    if(!json || !cJSON_ReplaceItemInObjectCaseSensitive(json,"stream",cJSON_CreateFalse())) goto done;
    body=cJSON_PrintUnformatted(json);
    if(!body || local_json(port,L"/v1/chat/completions",body,cancel,&wire,error,sizeof(error))!=LocalJsonOk) {
        fprintf(stderr,"generate: %s\n",error); goto done;
    }
    response=cJSON_Parse(wire); usage=cJSON_GetObjectItem(response,"usage");
    tokens=cJSON_GetObjectItem(usage,"prompt_tokens");
    printf("Thinking mode %d; saved exchanges: %zu; excluded: %zu; budgeted prompt: %d; reported prompt: %d; context: %d; reserve: %d; thinking switch: %d\n",
        thinking,c.count/2,budget.omitted_messages/2,budget.prompt_tokens,cJSON_IsNumber(tokens)?tokens->valueint:-1,
        budget.context_tokens,budget.response_tokens,budget.thinking_supported);
    ok=cJSON_IsNumber(tokens) && tokens->valueint==budget.prompt_tokens && c.count==200 &&
        budget.omitted_messages>0 && budget.omitted_messages%2==0 &&
        budget.prompt_tokens+budget.response_tokens+32<=budget.context_tokens;
    if(ok && thinking==ThinkingOff) {
        ReplyReport report;
        char *answer=NULL;
        int result=local_stream_report(port,request,cancel,NULL,NULL,&answer,&report,error,sizeof(error));
        printf("Streaming result: %d; prompt tokens: %d; completion tokens: %d; finish: %d\n",
            result,report.prompt_tokens,report.tokens,report.finish);
        ok=result==1 && answer && report.prompt_tokens==budget.prompt_tokens && report.tokens>0;
        free(answer);
    }
done:
    free(request); free(wire); free(body); cJSON_Delete(json); cJSON_Delete(response);
    CloseHandle(cancel); conversation_clear(&c);
    return ok?0:1;
}

int main(int argc,char **argv)
{
    int mode;
    if(argc!=2) return 2;
    for(mode=ThinkingAuto;mode<=ThinkingOn;mode++)
        if(check((unsigned short)atoi(argv[1]),mode)) return 1;
    return 0;
}

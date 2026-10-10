#include "context_win.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static cJSON *fetch(unsigned short port,const wchar_t *path,const char *body,HANDLE cancel,int *oversized,char *error,size_t capacity)
{
    char *wire=NULL;
    cJSON *json;
    int status=local_json(port,path,body,cancel,&wire,error,capacity);
    if(oversized) *oversized=status==LocalJsonTooLarge;
    if(status!=LocalJsonOk) return NULL;
    json=cJSON_ParseWithLengthOpts(wire,strlen(wire)+1,NULL,1); free(wire);
    if(!cJSON_IsObject(json)) { cJSON_Delete(json); snprintf(error,capacity,"Invalid JSON from the model information/token endpoint."); return NULL; }
    return json;
}

static int count_request(unsigned short port,const char *request,HANDLE cancel,int *count,char *error,size_t capacity)
{
    int oversized=0;
    cJSON *formatted=fetch(port,L"/apply-template",request,cancel,&oversized,error,capacity);
    cJSON *prompt=cJSON_GetObjectItemCaseSensitive(formatted,"prompt"),*body=NULL,*result=NULL,*tokens,*t;
    char *wire=NULL;
    int ok=0;
    if(!formatted) return oversized?2:0;
    if(!cJSON_IsString(prompt)) { snprintf(error,capacity,"The engine could not format this conversation with its chat template."); goto done; }
    body=cJSON_CreateObject();
    /* Generation tokenizes the formatted prompt with add_special=true, parse_special=true. */
    if(!body || !cJSON_AddStringToObject(body,"content",prompt->valuestring) ||
        !cJSON_AddBoolToObject(body,"add_special",1) || !cJSON_AddBoolToObject(body,"parse_special",1)) goto done;
    wire=cJSON_PrintUnformatted(body);
    if(!wire) goto done;
    if(strlen(wire)>LcbMaxWire) { oversized=1; snprintf(error,capacity,"Formatted prompt exceeds the request size limit."); goto done; }
    result=fetch(port,L"/tokenize",wire,cancel,&oversized,error,capacity);
    tokens=cJSON_GetObjectItemCaseSensitive(result,"tokens");
    if(!cJSON_IsArray(tokens)) { if(result) snprintf(error,capacity,"The engine returned an invalid token count."); goto done; }
    *count=0;
    cJSON_ArrayForEach(t,tokens) {
        if(!cJSON_IsNumber(t) || t->valuedouble<0 || t->valuedouble>INT_MAX || t->valuedouble!=t->valueint) goto done;
        (*count)++;
    }
    ok=1;
done:
    free(wire); cJSON_Delete(body); cJSON_Delete(result); cJSON_Delete(formatted);
    if(!ok && !error[0]) snprintf(error,capacity,"Cannot count the formatted prompt tokens.");
    return oversized?2:ok;
}

typedef struct CounterContext { unsigned short port; HANDLE cancel; } CounterContext;
static int count_callback(const char *wire,void *context,int *count,char *error,size_t capacity)
{
    CounterContext *c=context;
    if(c->cancel && WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0) {
        snprintf(error,capacity,"Stopped by user."); return CountFailed;
    }
    return count_request(c->port,wire,c->cancel,count,error,capacity);
}

int local_prepare(unsigned short port,const wchar_t *expected_model,const Conversation *c,
    const Module *module,const char *prompt,const Generation *settings,HANDLE cancel,
    char **request,ContextBudget *budget,char *error,size_t capacity)
{
    cJSON *props=NULL,*defaults,*context,*template,*build,*path,*caps;
    int ok=0;
    FitBudget fitted;
    CounterContext counter={port,cancel};
    wchar_t model[MAX_PATH];
    *request=NULL; memset(budget,0,sizeof(*budget));
    if(!capacity) return 0;
    error[0]=0;
    if(!generation_valid(settings) || c->count%2 || !prompt[0] || strlen(prompt)>LcbMaxPrompt) {
        snprintf(error,capacity,"Invalid generation settings or message (maximum 16 KiB per prompt)."); return 0;
    }
    props=fetch(port,L"/props",NULL,cancel,NULL,error,capacity);
    if(!props) goto done;
    defaults=cJSON_GetObjectItemCaseSensitive(props,"default_generation_settings");
    context=cJSON_GetObjectItemCaseSensitive(defaults,"n_ctx");
    template=cJSON_GetObjectItemCaseSensitive(props,"chat_template");
    build=cJSON_GetObjectItemCaseSensitive(props,"build_info");
    path=cJSON_GetObjectItemCaseSensitive(props,"model_path");
    if(!cJSON_IsNumber(context) || context->valuedouble<1 || context->valuedouble>INT_MAX || context->valuedouble!=context->valueint) {
        snprintf(error,capacity,"The engine did not report its loaded context size. Use a compatible llama.cpp server."); goto done;
    }
    if(!cJSON_IsString(template) || !template->valuestring[0]) {
        snprintf(error,capacity,"No chat template was reported. Select an instruction model with a valid embedded template."); goto done;
    }
    if(expected_model && expected_model[0] && (!cJSON_IsString(path) ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path->valuestring,-1,model,MAX_PATH) ||
        _wcsicmp(model,expected_model))) {
        snprintf(error,capacity,"The local server is running a different model. Select its GGUF file so the correct settings are used."); goto done;
    }
    budget->context_tokens=context->valueint;
    budget->response_tokens=settings->max_tokens;
    /* Effort control is a capability, not a generic reasoning detector.
       Missing/false does not rule out models that always emit reasoning. */
    caps=cJSON_GetObjectItemCaseSensitive(props,"chat_template_caps");
    budget->reasoning_effort_supported=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps,"supports_reasoning_effort"));
    if(cJSON_IsString(build)) snprintf(budget->build,sizeof(budget->build),"%s",build->valuestring);
    ok=conversation_fit(c,module,prompt,settings,budget->context_tokens,
        budget->reasoning_effort_supported,count_callback,&counter,request,&fitted,error,capacity);
    if(ok) { budget->prompt_tokens=fitted.prompt_tokens; budget->omitted_messages=fitted.omitted_messages; }

done:
    cJSON_Delete(props);
    if(!ok) { free(*request); *request=NULL; }
    return ok;
}

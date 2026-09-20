#define WIN32_LEAN_AND_MEAN
#include "stream_win.h"
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Transfer { HANDLE event, closed; DWORD error, bytes; } Transfer;
static void CALLBACK completed(HINTERNET h,DWORD_PTR context,DWORD status,void *info,DWORD length)
{
    Transfer *t=(Transfer *)context;
    (void)h;
    if(!t) return;
    if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { SetEvent(t->closed); return; }
    if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        t->error=((WINHTTP_ASYNC_RESULT *)info)->dwError; SetEvent(t->event);
    } else if(status==WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE) {
        t->bytes=*(DWORD *)info; SetEvent(t->event);
    } else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        t->bytes=length; SetEvent(t->event);
    } else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
        SetEvent(t->event);
}
static int ready(Transfer *t,HANDLE cancel,BOOL started)
{
    HANDLE events[2]={cancel,t->event};
    DWORD result;
    if(!started && GetLastError()!=ERROR_IO_PENDING) return 0;
    result=WaitForMultipleObjects(2,events,FALSE,90000);
    return result==WAIT_OBJECT_0+1 && !t->error;
}
int local_stream(unsigned short port,const char *body,HANDLE cancel,StreamUpdate update,
    void *context,char **answer,char *error,size_t capacity)
{
    return local_stream_report(port,body,cancel,update,context,answer,NULL,error,capacity);
}
static size_t utf8prefix(const char *text, size_t length)
{
    size_t i=0, n;
    while(i<length) {
        unsigned char ch=(unsigned char)text[i];
        if(ch<0x80) { i++; continue; }
        n=ch>=0xc2 && ch<=0xdf?2:ch>=0xe0 && ch<=0xef?3:ch>=0xf0 && ch<=0xf4?4:0;
        if(!n || n>length-i || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text+i,(int)n,NULL,0)) break;
        i+=n;
    }
    return i;
}
int local_stream_report(unsigned short port,const char *body,HANDLE cancel,StreamUpdate update,
    void *context,char **answer,ReplyReport *report,char *error,size_t capacity)
{
    HINTERNET session=NULL,connection=NULL,request=NULL;
    Transfer t={0};
    StreamReply *reply=calloc(1,sizeof(*reply));
    char buffer[8192];
    DWORD flags,status=0,n=sizeof(status);
    DWORD_PTR ctx=(DWORD_PTR)&t;
    int ok=0,callback=0;
    ULONGLONG lastupdate=0;
    *answer=NULL;
    if(report) memset(report,0,sizeof(*report));
    if(!capacity) { free(reply); return 0; }
    error[0]=0;
    if(!port || !cancel || !reply || strlen(body)>LcbMaxWire) goto done;
    t.event=CreateEventW(NULL,FALSE,FALSE,NULL); t.closed=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!t.event || !t.closed) goto done;
    session=WinHttpOpen(L"lcb-ai/0.5.0-pre.2",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,WINHTTP_FLAG_ASYNC);
    if(!session) goto done;
    WinHttpSetTimeouts(session,3000,3000,10000,90000);
    connection=WinHttpConnect(session,L"127.0.0.1",port,0);
    if(!connection) goto done;
    request=WinHttpOpenRequest(connection,L"POST",L"/v1/chat/completions",NULL,NULL,NULL,0);
    if(!request) goto done;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_CONTEXT_VALUE,&ctx,sizeof(ctx))) goto done;
    if(WinHttpSetStatusCallback(request,completed,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|
        WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK) goto done;
    callback=1;
    flags=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&flags,sizeof(flags))) goto done;
    flags=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&flags,sizeof(flags))) goto done;
    if(!ready(&t,cancel,WinHttpSendRequest(request,L"Content-Type: application/json\r\nAccept: text/event-stream\r\n",
        (DWORD)-1,(void *)body,(DWORD)strlen(body),(DWORD)strlen(body),ctx))) goto done;
    if(!ready(&t,cancel,WinHttpReceiveResponse(request,NULL))) goto done;
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&n,NULL)) goto done;
    if(status!=200) { snprintf(error,capacity,"Local engine returned HTTP %lu.",(unsigned long)status); goto done; }
    while(!reply->done) {
        size_t before=reply->used;
        DWORD available;
        if(!ready(&t,cancel,WinHttpQueryDataAvailable(request,NULL))) goto done;
        available=t.bytes;
        if(!available) break;
        if(available>sizeof(buffer)) available=sizeof(buffer);
        if(!ready(&t,cancel,WinHttpReadData(request,buffer,available,NULL))) goto done;
        if(!t.bytes) break;
        if(!stream_feed(reply,buffer,t.bytes)) { snprintf(error,capacity,"Invalid or oversized streaming response."); goto done; }
        if(update && reply->used>before && (reply->done || GetTickCount64()-lastupdate>=40)) {
            update(reply->text,context); lastupdate=GetTickCount64();
        }
    }
    if(reply->done && reply->used) ok=1;
    else if(reply->done && reply->finish==FinishLength && reply->saw_reasoning)
        snprintf(error,capacity,"Response limit reached during thinking. Increase Response tokens or set Thinking to Off. Your draft was retained.");
    else if(reply->done && !reply->used)
        snprintf(error,capacity,"The model returned no answer text. Try a larger response limit or different thinking setting.");
    else snprintf(error,capacity,"The engine ended the stream before a complete answer arrived.");
done:
    if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0) ok=2;
    /* Wait for the final callback before freeing its context and read buffer. */
    if(request) { WinHttpCloseHandle(request); if(callback) WaitForSingleObject(t.closed,INFINITE); }
    if(connection) WinHttpCloseHandle(connection);
    if(session) WinHttpCloseHandle(session);
    if(t.event) CloseHandle(t.event);
    if(t.closed) CloseHandle(t.closed);
    if(reply && reply->used) {
        size_t valid=utf8prefix(reply->text,reply->used);
        if(valid<reply->used) {
            reply->text[valid]=0; reply->used=valid; ok=0;
            snprintf(error,capacity,"The engine returned invalid UTF-8 text.");
        }
    }
    if(reply && reply->used) {
        *answer=malloc(reply->used+1);
        if(*answer) memcpy(*answer,reply->text,reply->used+1); else ok=0;
    }
    if(!ok && !error[0]) snprintf(error,capacity,"The local streaming request failed or timed out.");
    if(report && reply) {
        report->prompt_tokens=reply->prompt_tokens; report->tokens=reply->tokens;
        report->finish=reply->finish; report->saw_reasoning=reply->saw_reasoning;
    }
    free(reply); return ok;
}

int local_json(unsigned short port, const wchar_t *path, const char *body,
    HANDLE cancel, char **answer, char *error, size_t capacity)
{
    HINTERNET session=NULL,connection=NULL,request=NULL;
    Transfer t={0};
    DWORD_PTR ctx=(DWORD_PTR)&t;
    DWORD flags,status=0,n=sizeof(status);
    size_t used=0,length=body?strlen(body):0;
    char *wire=NULL;
    int callback=0,ok=0;
    ULONGLONG started=GetTickCount64();
    *answer=NULL;
    if(!capacity) return 0;
    error[0]=0;
    if(length>LcbMaxWire) { ok=LocalJsonTooLarge; snprintf(error,capacity,"Local JSON request exceeds 1 MiB."); goto done; }
    if(!port || !cancel) goto done;
    wire=malloc(LcbMaxWire+1);
    t.event=CreateEventW(NULL,FALSE,FALSE,NULL); t.closed=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!wire || !t.event || !t.closed) goto done;
    session=WinHttpOpen(L"lcb-ai/0.5.0-pre.2",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,WINHTTP_FLAG_ASYNC);
    if(!session) goto done;
    WinHttpSetTimeouts(session,3000,3000,10000,30000);
    connection=WinHttpConnect(session,L"127.0.0.1",port,0);
    if(!connection) goto done;
    request=WinHttpOpenRequest(connection,body?L"POST":L"GET",path,NULL,NULL,NULL,0);
    if(!request || !WinHttpSetOption(request,WINHTTP_OPTION_CONTEXT_VALUE,&ctx,sizeof(ctx))) goto done;
    if(WinHttpSetStatusCallback(request,completed,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|
        WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK) goto done;
    callback=1;
    flags=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&flags,sizeof(flags))) goto done;
    flags=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&flags,sizeof(flags))) goto done;
    if(!ready(&t,cancel,WinHttpSendRequest(request,L"Content-Type: application/json\r\n",(DWORD)-1,
        (void *)body,(DWORD)length,(DWORD)length,ctx)) ||
        !ready(&t,cancel,WinHttpReceiveResponse(request,NULL))) goto done;
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&n,NULL)) goto done;
    if(status!=200) { snprintf(error,capacity,"Engine metadata/token endpoint returned HTTP %lu. Check engine compatibility and template.",(unsigned long)status); goto done; }
    for(;;) {
        DWORD available;
        if(GetTickCount64()-started>60000) goto done;
        if(!ready(&t,cancel,WinHttpQueryDataAvailable(request,NULL))) goto done;
        available=t.bytes;
        if(!available) break;
        if(available>LcbMaxWire-used) { ok=LocalJsonTooLarge; snprintf(error,capacity,"Engine metadata/token response exceeds 1 MiB."); goto done; }
        if(!ready(&t,cancel,WinHttpReadData(request,wire+used,available,NULL))) goto done;
        if(!t.bytes) break;
        used+=t.bytes;
    }
    if(!used || memchr(wire,0,used)) goto done;
    wire[used]=0; *answer=wire; wire=NULL; ok=1;
done:
    if(request) { WinHttpCloseHandle(request); if(callback) WaitForSingleObject(t.closed,INFINITE); }
    if(connection) WinHttpCloseHandle(connection);
    if(session) WinHttpCloseHandle(session);
    if(t.event) CloseHandle(t.event);
    if(t.closed) CloseHandle(t.closed);
    free(wire);
    if(!ok && !error[0]) snprintf(error,capacity,"Cannot inspect the local model or count tokens; request stopped, unavailable, or timed out.");
    return ok;
}

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
    HINTERNET session=NULL,connection=NULL,request=NULL;
    Transfer t={0};
    StreamReply *reply=calloc(1,sizeof(*reply));
    char buffer[8192];
    DWORD flags,status=0,n=sizeof(status);
    DWORD_PTR ctx=(DWORD_PTR)&t;
    int ok=0,callback=0;
    ULONGLONG lastupdate=0;
    *answer=NULL;
    if(!capacity) { free(reply); return 0; }
    error[0]=0;
    if(!port || !cancel || !reply || strlen(body)>LtsMaxWire) goto done;
    t.event=CreateEventW(NULL,FALSE,FALSE,NULL); t.closed=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!t.event || !t.closed) goto done;
    session=WinHttpOpen(L"lts-ai/0.4.0",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,WINHTTP_FLAG_ASYNC);
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
    else snprintf(error,capacity,"The engine ended the stream before a complete answer arrived.");
done:
    if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0) ok=2;
    /* Wait for the final callback before freeing its context and read buffer. */
    if(request) { WinHttpCloseHandle(request); if(callback) WaitForSingleObject(t.closed,INFINITE); }
    if(connection) WinHttpCloseHandle(connection);
    if(session) WinHttpCloseHandle(session);
    if(t.event) CloseHandle(t.event);
    if(t.closed) CloseHandle(t.closed);
    if(ok && reply && reply->used) {
        *answer=malloc(reply->used+1);
        if(*answer) memcpy(*answer,reply->text,reply->used+1); else ok=0;
    }
    if(!ok && !error[0]) snprintf(error,capacity,"The local streaming request failed or timed out.");
    free(reply); return ok;
}

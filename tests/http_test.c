#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcb.h"
#include "engine_win.h"
#include "cJSON.h"
#include "stream_win.h"
#include "context_win.h"

typedef struct Server Server;
struct Server {
	SOCKET socket;
	HANDLE thread, stop;
	unsigned short port;
	int status, requests, delay, contextmode;
	const char *body;
	char request[65536];
};

static char *context_response(Server *s,const char *request)
{
    cJSON *body=NULL,*reply=cJSON_CreateObject(),*messages,*prompt,*tokens;
    char *wire,*formatted=NULL;
    const char *text=strstr(request,"\r\n\r\n");
    assert(reply && text);
    if(!strncmp(request,"GET /props ",11)) {
        cJSON_Delete(reply);
        reply=cJSON_Parse(s->body); assert(reply);
    } else {
        body=cJSON_Parse(text+4); assert(body);
        if(!strncmp(request,"POST /apply-template ",21)) {
            cJSON *kwargs=cJSON_GetObjectItem(body,"chat_template_kwargs");
            if(s->contextmode==2) assert(cJSON_IsFalse(cJSON_GetObjectItem(kwargs,"enable_thinking")));
            else assert(!kwargs);
            messages=cJSON_GetObjectItem(body,"messages");
            assert(cJSON_IsArray(messages));
            assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages,0),"role")->valuestring,"system"));
            formatted=cJSON_PrintUnformatted(messages); assert(formatted);
            assert(cJSON_AddStringToObject(reply,"prompt",formatted));
        } else {
            size_t i;
            assert(!strncmp(request,"POST /tokenize ",15));
            assert(cJSON_IsTrue(cJSON_GetObjectItem(body,"add_special")));
            assert(cJSON_IsTrue(cJSON_GetObjectItem(body,"parse_special")));
            prompt=cJSON_GetObjectItem(body,"content"); assert(cJSON_IsString(prompt));
            if(s->contextmode==3 && strlen(prompt->valuestring)>600) {
                wire=malloc(LcbMaxWire+2); assert(wire);
                memset(wire,'x',LcbMaxWire+1); wire[LcbMaxWire+1]=0;
                cJSON_Delete(body); cJSON_Delete(reply); return wire;
            }
            tokens=cJSON_AddArrayToObject(reply,"tokens"); assert(tokens);
            /* Deterministic fake tokenizer, deliberately including JSON/template overhead. */
            for(i=0;i<strlen(prompt->valuestring);i++) assert(cJSON_AddItemToArray(tokens,cJSON_CreateNumber(42)));
        }
    }
    wire=cJSON_PrintUnformatted(reply); assert(wire);
    free(formatted); cJSON_Delete(body); cJSON_Delete(reply); return wire;
}

static void
sendall(SOCKET socket, const char *text, size_t length)
{
	int sent;
	while(length) {
		sent = send(socket, text, (int)length, 0);
		if(sent <= 0) return;
		text += sent; length -= (size_t)sent;
	}
}

static DWORD WINAPI
serve(void *arg)
{
	Server *s = arg;
	SOCKET client;
	fd_set ready;
	struct timeval wait;
	DWORD timeout = 3000;
	char request[65536], header[256], *end, *length;
	char *dynamic;
	const char *body;
	size_t used, expected;
	int n;
	while(WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT) {
		FD_ZERO(&ready); FD_SET(s->socket, &ready);
		wait.tv_sec = 0; wait.tv_usec = 100000;
		if(select(0, &ready, NULL, NULL, &wait) <= 0) continue;
		client = accept(s->socket, NULL, NULL);
		assert(client != INVALID_SOCKET);
		assert(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) == 0);
		assert(setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)) == 0);
		used = 0;
		for(;;) {
			assert(used < sizeof(request)-1);
			n = recv(client, request+used, (int)(sizeof(request)-1-used), 0);
			assert(n > 0);
			used += (size_t)n; request[used] = 0;
			end = strstr(request, "\r\n\r\n");
			if(!end) continue;
			expected = (size_t)(end+4-request);
			length = strstr(request, "Content-Length:");
			if(length && length < end) expected += strtoul(length+15, NULL, 10);
			assert(expected < sizeof(request));
			if(used >= expected) break;
		}
		if(s->requests++ == 0) memcpy(s->request, request, used+1);
		dynamic=s->contextmode?context_response(s,request):NULL;
		body=dynamic?dynamic:s->body;
		n = snprintf(header, sizeof(header), "HTTP/1.1 %d Test\r\nContent-Length: %zu\r\n"
		    "Content-Type: application/json\r\nConnection: close\r\n%s\r\n",
		    s->status, strlen(body), s->status == 302 ? "Location: /forbidden\r\n" : "");
		assert(n > 0 && (size_t)n < sizeof(header));
		sendall(client, header, (size_t)n);
        if(s->delay==1) {
            const char *split=strstr(body,"\n\n");
            size_t first=split?(size_t)(split+2-body):0;
            sendall(client,body,first);
            WaitForSingleObject(s->stop,2000);
            sendall(client,body+first,strlen(body)-first);
        } else {
            if(s->delay==2) WaitForSingleObject(s->stop,2000);
            sendall(client,body,strlen(body));
        }
		closesocket(client);
		free(dynamic);
	}
	return 0;
}

static void
start_delayed(Server *s, int status, const char *body, int delay)
{
	struct sockaddr_in address = {0};
	int size = sizeof(address);
	BOOL exclusive = TRUE;
	memset(s, 0, sizeof(*s));
	s->status = status; s->body = body; s->delay=delay;
	s->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	assert(s->socket != INVALID_SOCKET);
	assert(setsockopt(s->socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive)) == 0);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(bind(s->socket, (struct sockaddr *)&address, sizeof(address)) == 0);
	assert(getsockname(s->socket, (struct sockaddr *)&address, &size) == 0);
	s->port = ntohs(address.sin_port);
	assert(listen(s->socket, 4) == 0);
	s->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
	assert(s->stop != NULL);
	s->thread = CreateThread(NULL, 0, serve, s, 0, NULL);
	assert(s->thread != NULL);
}

static void start(Server *s,int status,const char *body) { start_delayed(s,status,body,0); }

static void
stop(Server *s, int requests)
{
	assert(SetEvent(s->stop));
	assert(WaitForSingleObject(s->thread, 5000) == WAIT_OBJECT_0);
	CloseHandle(s->thread); CloseHandle(s->stop); closesocket(s->socket);
	assert(requests<0 || s->requests == requests);
}

static void budgeting(void)
{
    const char *props="{\"default_generation_settings\":{\"n_ctx\":512},\"chat_template\":\"{{ enable_thinking }}\",\"build_info\":\"test\"}";
    Server s;
    Conversation c={0}; Generation g=generation_defaults();
    Module module={"Test","Always retain these instructions."};
    ContextBudget budget;
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    char *request=NULL,error[256],large[601];
    cJSON *json,*messages;
    size_t i;
    g.max_tokens=128;
    for(i=0;i<40;i++) {
        assert(conversation_add(&c,"user","Question with Unicode \xe2\x98\xba"));
        assert(conversation_add(&c,"assistant","Answer with newline\nand caf\xc3\xa9"));
    }
    start(&s,200,props); s.contextmode=1;
    assert(local_prepare(s.port,NULL,&c,&module,"Current question",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(budget.context_tokens==512 && budget.prompt_tokens+g.max_tokens+32<=512);
    assert(budget.omitted_messages>0 && budget.omitted_messages<80 && budget.omitted_messages%2==0);
    assert(c.count==80 && !strcmp(c.messages[0].text,"Question with Unicode \xe2\x98\xba"));
    json=cJSON_Parse(request); assert(json); free(request);
    messages=cJSON_GetObjectItem(json,"messages");
    assert((size_t)cJSON_GetArraySize(messages)==c.count-budget.omitted_messages+2);
    assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages,cJSON_GetArraySize(messages)-1),"content")->valuestring,"Current question"));
    cJSON_Delete(json); stop(&s,-1);
    /* More than 16 exchanges are actually transmitted when context permits. */
    start(&s,200,"{\"default_generation_settings\":{\"n_ctx\":16384},\"chat_template\":\"{{ enable_thinking }}\"}"); s.contextmode=1;
    assert(local_prepare(s.port,NULL,&c,&module,"Current question",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(budget.omitted_messages==0);
    json=cJSON_Parse(request); free(request);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(json,"messages"))==82);
    cJSON_Delete(json); stop(&s,-1);
    /* An oversized tokenizer response for a large candidate should cause
       further trimming, not prevent an otherwise valid small request. */
    start(&s,200,props); s.contextmode=3;
    assert(local_prepare(s.port,NULL,&c,&module,"Current question",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(budget.omitted_messages>0 && budget.prompt_tokens+g.max_tokens+32<=512 && !error[0]);
    free(request); stop(&s,-1);
    /* Supported override is identical during budgeting and generation. */
    g.thinking=ThinkingOff;
    start(&s,200,props); s.contextmode=2;
    assert(local_prepare(s.port,NULL,&c,&module,"Current question",&g,cancel,&request,&budget,error,sizeof(error)));
    json=cJSON_Parse(request); free(request);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(json,"chat_template_kwargs"),"enable_thinking")));
    cJSON_Delete(json); stop(&s,-1);
    g.thinking=ThinkingAuto;
    memset(large,'x',600); large[600]=0;
    start(&s,200,props); s.contextmode=1;
    assert(!local_prepare(s.port,NULL,&c,&module,large,&g,cancel,&request,&budget,error,sizeof(error)));
    assert(!request && strstr(error,"current message") && c.count==80); stop(&s,3);
    g.max_tokens=512;
    start(&s,200,props); s.contextmode=1;
    assert(!local_prepare(s.port,NULL,&c,&module,"test",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(strstr(error,"no prompt room")); stop(&s,1);
    g.max_tokens=128; g.thinking=ThinkingOn;
    start(&s,200,"{\"default_generation_settings\":{\"n_ctx\":512},\"chat_template\":\"plain template\"}"); s.contextmode=1;
    assert(!local_prepare(s.port,NULL,&c,&module,"test",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(strstr(error,"Auto")); stop(&s,1);
    g.thinking=ThinkingAuto;
    start(&s,200,"{\"default_generation_settings\":{\"n_ctx\":512},\"chat_template\":\"\"}"); s.contextmode=1;
    assert(!local_prepare(s.port,NULL,&c,&module,"test",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(strstr(error,"No chat template")); stop(&s,1);
    start(&s,200,props); s.contextmode=1;
    assert(!local_prepare(s.port,L"C:\\different.gguf",&c,&module,"test",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(strstr(error,"different model")); stop(&s,1);
    start(&s,404,"{}");
    assert(!local_prepare(s.port,NULL,&c,&module,"test",&g,cancel,&request,&budget,error,sizeof(error)));
    assert(strstr(error,"404")); stop(&s,1);
    CloseHandle(cancel); conversation_clear(&c);
    puts("Context tests passed: 40 exchanges, actual formatted tokens, whole-turn trimming, Unicode, thinking, oversized prompts, mismatched model, missing template/endpoints.");
}

static void
transport(void)
{
	Server s;
	Conversation c = {0};
	const char *prompt = "a \"quoted\" prompt\nwith newline";
	const char *reply = "{\"choices\":[{\"message\":{\"content\":\"Local reply \\u263a\"}}]}";
	char *request, *answer, *huge, error[256];
	cJSON *root, *messages, *value;
	struct { int status; const char *body, *error; } cases[4];
	size_t i;
	request = conversation_request(&c, &lcb_modules[0], prompt);
	assert(request != NULL);
	start(&s, 200, reply);
	assert(local_complete(s.port, request, &answer, error, sizeof(error)));
	stop(&s, 1);
	assert(strcmp(answer, "Local reply \xe2\x98\xba") == 0); free(answer);
	assert(strncmp(s.request, "POST /v1/chat/completions HTTP/1.1\r\n", 36) == 0);
	root = cJSON_Parse(strstr(s.request, "\r\n\r\n")+4); assert(root != NULL);
	assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "stream")));
	messages = cJSON_GetObjectItemCaseSensitive(root, "messages");
	value = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(messages, cJSON_GetArraySize(messages)-1), "content");
	assert(cJSON_IsString(value) && strcmp(value->valuestring, prompt) == 0);
	cJSON_Delete(root);
	huge = malloc(LcbMaxWire+2); assert(huge != NULL);
	memset(huge, 'x', LcbMaxWire+1); huge[LcbMaxWire+1] = 0;
	cases[0].status=503; cases[0].body=reply; cases[0].error="503";
	cases[1].status=200; cases[1].body="{\"choices\":"; cases[1].error="invalid";
	cases[2].status=200; cases[2].body=huge; cases[2].error="1 MiB";
	cases[3].status=302; cases[3].body=""; cases[3].error="302";
	for(i=0; i<4; i++) {
		start(&s, cases[i].status, cases[i].body);
		assert(!local_complete(s.port, request, &answer, error, sizeof(error)));
		stop(&s, 1);
		assert(answer == NULL && strstr(error, cases[i].error) != NULL);
	}
	free(huge); free(request);
}

typedef struct Updates { int count; HANDLE cancel; } Updates;
static void updated(const char *text,void *context)
{
    Updates *u=context;
    assert(strstr(text,"Hello")!=NULL); u->count++;
    if(u->cancel) SetEvent(u->cancel);
}
static DWORD WINAPI cancelsoon(void *context) { Sleep(100); SetEvent((HANDLE)context); return 0; }
static void streamtransport(void)
{
    const char *wire="data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
        "data: [DONE]\n\n";
    Server s;
    Conversation c={0}; Generation g={128,0.5,0.9,4096,ThinkingAuto,1,0,20,0,0};
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL),thread;
    Updates u={0};
    char *request=conversation_generate(&c,&lcb_modules[0],"hello",&g),*answer,error[256];
    ULONGLONG begin;
    assert(cancel && request);
    start(&s,200,wire);
    assert(local_stream(s.port,request,cancel,updated,&u,&answer,error,sizeof(error))==1);
    assert(u.count>0 && !strcmp(answer,"Hello world")); free(answer); stop(&s,1);
    start_delayed(&s,200,wire,1); u.cancel=cancel;
    begin=GetTickCount64();
    assert(local_stream(s.port,request,cancel,updated,&u,&answer,error,sizeof(error))==2);
    assert(GetTickCount64()-begin<1500 && answer && !strcmp(answer,"Hello")); free(answer); stop(&s,1);
    ResetEvent(cancel); u.cancel=NULL;
    start_delayed(&s,200,wire,2);
    thread=CreateThread(NULL,0,cancelsoon,cancel,0,NULL); assert(thread);
    begin=GetTickCount64();
    assert(local_stream(s.port,request,cancel,updated,&u,&answer,error,sizeof(error))==2);
    assert(GetTickCount64()-begin<1500 && !answer); stop(&s,1);
    assert(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0); CloseHandle(thread); ResetEvent(cancel);
    start(&s,200,"data: invalid\n\n");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error))); assert(!answer); stop(&s,1);
    start(&s,503,"{}");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error))); assert(strstr(error,"503")); stop(&s,1);
    start(&s,200,"data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error)));
    assert(answer && !strcmp(answer,"Hello") && strstr(error,"ended the stream")); free(answer); stop(&s,1);
    start(&s,200,"data: {\"choices\":[{\"delta\":{\"content\":\"caf\xc3\xa9\"}}]}\n\n"
        "data: {broken\n\n");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error)));
    assert(answer && !strcmp(answer,"caf\xc3\xa9")); free(answer); stop(&s,1);
    start(&s,200,"data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n"
        "data: {\"error\":{\"message\":\"Engine failed\"}}\n\n");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error)));
    assert(answer && !strcmp(answer,"Hello")); free(answer); stop(&s,1);
    start(&s,200,"data: {\"choices\":[{\"delta\":{\"content\":\"Hello \xe2\x82\"}}]}\n\n"
        "data: [DONE]\n\n");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error)));
    assert(answer && !strcmp(answer,"Hello ") && strstr(error,"UTF-8")); free(answer); stop(&s,1);
    start(&s,200,"data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Let me think\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n\n"
        "data: [DONE]\n\n");
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error)));
    assert(!answer && strstr(error,"during thinking")); stop(&s,1);
    /* Stop also interrupts metadata/token counting, before any generation. */
    start_delayed(&s,200,"{\"tokens\":[1]}",2);
    thread=CreateThread(NULL,0,cancelsoon,cancel,0,NULL); assert(thread);
    begin=GetTickCount64();
    assert(!local_json(s.port,L"/tokenize","{}",cancel,&answer,error,sizeof(error)));
    assert(GetTickCount64()-begin<1500 && !answer); stop(&s,1);
    assert(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0); CloseHandle(thread);
    CloseHandle(cancel); free(request);
}

static void
health(void)
{
	Server s;
	EngineProcess p = {0};
	wchar_t exe[MAX_PATH], error[256];
	char huge[2049];
	struct { int status; const char *body; int expected; } cases[] = {
		{200, "{\"status\":\"ok\"}", EngineReady}, {503, "{}", EngineLoading},
		{200, "{\"status\":\"other\"}", EngineOther}, {200, "bad json", EngineOther},
		{200, huge, EngineOther}, {302, "", EngineOther}
	};
	size_t i;
	memset(huge, 'x', sizeof(huge)-1); huge[sizeof(huge)-1] = 0;
	for(i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		start(&s, cases[i].status, cases[i].body);
		assert(engine_probe(s.port) == cases[i].expected);
		stop(&s, 1);
		assert(strncmp(s.request, "GET /health HTTP/1.1\r\n", 22) == 0);
	}
	start(&s, 200, "{\"status\":\"ok\"}");
	assert(GetModuleFileNameW(NULL, exe, MAX_PATH) > 0);
	assert(!engine_start(&p, exe, exe, s.port, error, 256));
	assert(p.process == NULL && p.job == NULL && wcsstr(error, L"already in use") != NULL);
	assert(engine_probe(s.port) == EngineReady);
	stop(&s, 1);
	assert(engine_probe(s.port) == EngineOffline);
}

static void
ports(const char *path)
{
	const wchar_t *invalid[] = {L"0", L"65536", L"-1", L"8080x", L"https://example.com"};
	wchar_t exe[MAX_PATH], command[1024];
	STARTUPINFOW startup = {0};
	PROCESS_INFORMATION child;
	DWORD code;
	size_t i;
	assert(MultiByteToWideChar(CP_ACP, 0, path, -1, exe, MAX_PATH) > 0);
	startup.cb = sizeof(startup);
	for(i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
		assert(swprintf(command, 1024, L"\"%ls\" \"%ls\" hello", exe, invalid[i]) > 0);
		assert(CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &child));
		assert(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0);
		assert(GetExitCodeProcess(child.hProcess, &code) && code == 2);
		CloseHandle(child.hThread); CloseHandle(child.hProcess);
	}
}

int
main(int argc, char **argv)
{
	WSADATA data;
	assert(argc == 2);
	assert(WSAStartup(MAKEWORD(2,2), &data) == 0);
	transport(); streamtransport(); budgeting(); health(); ports(argv[1]);
	WSACleanup();
	puts("HTTP tests passed.");
	return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lts.h"
#include "engine_win.h"
#include "cJSON.h"
#include "stream_win.h"

typedef struct Server Server;
struct Server {
	SOCKET socket;
	HANDLE thread, stop;
	unsigned short port;
	int status, requests, delay;
	const char *body;
	char request[8192];
};

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
	char request[8192], header[256], *end, *length;
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
		n = snprintf(header, sizeof(header), "HTTP/1.1 %d Test\r\nContent-Length: %zu\r\n"
		    "Content-Type: application/json\r\nConnection: close\r\n%s\r\n",
		    s->status, strlen(s->body), s->status == 302 ? "Location: /forbidden\r\n" : "");
		assert(n > 0 && (size_t)n < sizeof(header));
		sendall(client, header, (size_t)n);
        if(s->delay==1) {
            const char *split=strstr(s->body,"\n\n");
            size_t first=split?(size_t)(split+2-s->body):0;
            sendall(client,s->body,first);
            WaitForSingleObject(s->stop,2000);
            sendall(client,s->body+first,strlen(s->body)-first);
        } else {
            if(s->delay==2) WaitForSingleObject(s->stop,2000);
            sendall(client,s->body,strlen(s->body));
        }
		closesocket(client);
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
	assert(s->requests == requests);
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
	request = conversation_request(&c, &lts_modules[0], prompt);
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
	huge = malloc(LtsMaxWire+2); assert(huge != NULL);
	memset(huge, 'x', LtsMaxWire+1); huge[LtsMaxWire+1] = 0;
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
    Conversation c={0}; Generation g={128,0.5,0.9};
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL),thread;
    Updates u={0};
    char *request=conversation_generate(&c,&lts_modules[0],"hello",&g),*answer,error[256];
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
    assert(!local_stream(s.port,request,cancel,NULL,NULL,&answer,error,sizeof(error))); assert(!answer); stop(&s,1);
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
	transport(); streamtransport(); health(); ports(argv[1]);
	WSACleanup();
	puts("HTTP tests passed.");
	return 0;
}

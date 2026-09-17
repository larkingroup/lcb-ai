#include "engine_win.h"
#include <winsock2.h>
#include <winhttp.h>
#include <wchar.h>
#include <string.h>
#include "cJSON.h"

int
engine_probe(unsigned short port)
{
	HINTERNET s = NULL, c = NULL, r = NULL;
	DWORD status = 0, n = sizeof(status), got, used = 0;
	DWORD flags = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	char body[2048];
	cJSON *json = NULL, *value;
	int result = EngineOffline;
	ULONGLONG start = GetTickCount64();
	s = WinHttpOpen(L"lts-ai/0.4.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
	if(!s) goto done;
	WinHttpSetTimeouts(s, 500, 500, 500, 800);
	c = WinHttpConnect(s, L"127.0.0.1", port, 0);
	if(!c) goto done;
	r = WinHttpOpenRequest(c, L"GET", L"/health", NULL, NULL, NULL, 0);
	if(!r || !WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &flags, sizeof(flags))) goto done;
	flags = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
	if(!WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &flags, sizeof(flags))) goto done;
	if(!WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) goto done;
	result = EngineOther;
	if(!WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	    NULL, &status, &n, NULL)) goto done;
	if(status == 503) { result = EngineLoading; goto done; }
	if(status != 200) goto done;
	do {
		if(GetTickCount64()-start > 3000) goto done;
		if(used == sizeof(body)-1 || !WinHttpReadData(r, body+used, (DWORD)sizeof(body)-1-used, &got)) goto done;
		used += got;
	} while(got);
	body[used] = 0;
	json = cJSON_ParseWithLengthOpts(body, used+1, NULL, 1);
	value = cJSON_GetObjectItemCaseSensitive(json, "status");
	if(cJSON_IsString(value) && strcmp(value->valuestring, "ok") == 0) result = EngineReady;
done:
	cJSON_Delete(json);
	if(r) WinHttpCloseHandle(r);
	if(c) WinHttpCloseHandle(c);
	if(s) WinHttpCloseHandle(s);
	return result;
}

static int
portfree(unsigned short port)
{
	WSADATA data;
	SOCKET s;
	struct sockaddr_in address;
	BOOL exclusive = TRUE;
	int ok = 0;
	if(WSAStartup(MAKEWORD(2,2), &data)) return 0;
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s != INVALID_SOCKET) {
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(port);
		if(setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive)) == 0)
			ok = bind(s, (struct sockaddr *)&address, sizeof(address)) == 0;
		closesocket(s);
	}
	WSACleanup();
	return ok;
}

void
engine_stop(EngineProcess *p)
{
	/* Only this client's job is affected, never an engine found on a port. */
	if(p->job) { CloseHandle(p->job); p->job = NULL; }
	if(p->process) { WaitForSingleObject(p->process, 3000); CloseHandle(p->process); p->process = NULL; }
}

int
engine_exited(EngineProcess *p)
{
	if(!p->process || WaitForSingleObject(p->process, 0) != WAIT_OBJECT_0) return 0;
	engine_stop(p);
	return 1;
}

int
engine_start(EngineProcess *p, const wchar_t *exe, const wchar_t *model,
    unsigned short port, wchar_t *error, size_t capacity)
{
	wchar_t command[4096], directory[MAX_PATH], *slash;
	STARTUPINFOW startup = {0};
	PROCESS_INFORMATION info = {0};
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
	int n;
	DWORD attributes;
	if(p->process) { swprintf(error, capacity, L"Unload the current model first."); return 0; }
	attributes = GetFileAttributesW(exe);
	if(attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
		swprintf(error, capacity, L"Choose the engine executable first."); return 0;
	}
	attributes = GetFileAttributesW(model);
	if(attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
		swprintf(error, capacity, L"Choose an available GGUF model file."); return 0;
	}
	/* Windows file names cannot contain quotes. Never interpolate through a shell. */
	if(wcschr(exe, L'"') || wcschr(model, L'"') || wcslen(exe) >= MAX_PATH ||
	    wcslen(model) >= MAX_PATH || port == 0) {
		swprintf(error, capacity, L"Invalid engine or model path."); return 0;
	}
	if(!portfree(port)) {
		swprintf(error, capacity, L"Port %u is already in use. The existing process was left alone.", port); return 0;
	}
	wcscpy(directory, exe);
	slash = wcsrchr(directory, L'\\');
	if(!slash) { swprintf(error, capacity, L"Use a full engine path."); return 0; }
	*slash = 0;
	n = swprintf(command, 4096, L"\"%ls\" --model \"%ls\" --alias local --host 127.0.0.1 --port %u "
	    L"--ctx-size 4096 --parallel 1 --fit on --gpu-layers auto --cache-type-k q8_0 "
	    L"--cache-type-v q8_0 --no-webui --no-agent --no-ui-mcp-proxy --cors-origins localhost --no-cors-credentials",
	    exe, model, port);
	if(n < 0 || n >= 4096) { swprintf(error, capacity, L"Engine command is too long."); return 0; }
	p->job = CreateJobObjectW(NULL, NULL);
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if(!p->job || !SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto failed;
	startup.cb = sizeof(startup);
	if(!CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
	    NULL, directory, &startup, &info)) goto failed;
	if(!AssignProcessToJobObject(p->job, info.hProcess)) {
		TerminateProcess(info.hProcess, 1); CloseHandle(info.hProcess); CloseHandle(info.hThread); goto failed;
	}
	p->process = info.hProcess;
	if(ResumeThread(info.hThread) == (DWORD)-1) {
		CloseHandle(info.hThread); engine_stop(p); goto failed;
	}
	CloseHandle(info.hThread);
	return 1;
failed:
	swprintf(error, capacity, L"Cannot start the local engine (Windows error %lu).", (unsigned long)GetLastError());
	engine_stop(p);
	return 0;
}

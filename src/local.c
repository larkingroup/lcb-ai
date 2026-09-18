#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcb.h"

const Engine lcb_local_engine = {"Local HTTP", local_complete};

int
local_complete(unsigned short port, const char *body, char **answer,
    char *error, size_t capacity)
{
	HINTERNET session = NULL, connection = NULL, request = NULL;
	DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, status = 0;
	DWORD statusbytes = sizeof(status), got, code = 0;
	char *wire = NULL;
	size_t used = 0, bodylen = strlen(body);
	ULONGLONG start = GetTickCount64();
	int ok = 0;
	*answer = NULL;
	if(capacity == 0)
		return 0;
	error[0] = 0;
	if(port == 0 || bodylen > LcbMaxWire) {
		snprintf(error, capacity, "Invalid local request.");
		return 0;
	}
	/* Literal loopback, no proxy, no redirects, no cookies or credentials. */
	session = WinHttpOpen(L"lcb-ai/0.5.0-pre.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
	    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(session == NULL)
		goto done;
	if(!WinHttpSetTimeouts(session, 3000, 3000, 10000, 90000))
		goto done;
	connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
	if(connection == NULL)
		goto done;
	request = WinHttpOpenRequest(connection, L"POST", L"/v1/chat/completions",
	    NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if(request == NULL || !WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
	    &policy, sizeof(policy)))
		goto done;
	policy = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
	if(!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &policy, sizeof(policy)))
		goto done;
	if(!WinHttpSendRequest(request, L"Content-Type: application/json\r\n", (DWORD)-1,
	    (void *)body, (DWORD)bodylen, (DWORD)bodylen, 0) ||
	    !WinHttpReceiveResponse(request, NULL) ||
	    !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusbytes, WINHTTP_NO_HEADER_INDEX))
		goto done;
	if(status != 200) {
		snprintf(error, capacity, "Local engine returned HTTP %lu. Check that the model is ready.", (unsigned long)status);
		goto done;
	}
	wire = malloc(LcbMaxWire + 1);
	if(wire == NULL) {
		snprintf(error, capacity, "Out of memory.");
		goto done;
	}
	for(;;) {
		if(GetTickCount64() - start > 120000) {
			snprintf(error, capacity, "Local engine response deadline exceeded.");
			goto done;
		}
		if(!WinHttpReadData(request, wire + used,
		    (DWORD)((LcbMaxWire + 1 - used) > 8192 ? 8192 : (LcbMaxWire + 1 - used)), &got))
			goto done;
		used += got;
		if(used > LcbMaxWire) {
			snprintf(error, capacity, "Local engine response exceeds 1 MiB.");
			goto done;
		}
		if(got == 0)
			break;
	}
	wire[used] = 0;
	ok = response_parse(wire, used, answer);
	if(!ok)
		snprintf(error, capacity, "Local engine returned an invalid or oversized text answer.");
done:
	code = GetLastError();
	if(!ok && error[0] == 0)
		snprintf(error, capacity, "Cannot reach local engine on 127.0.0.1:%u (Windows error %lu).", port, (unsigned long)code);
	free(wire);
	if(request != NULL) WinHttpCloseHandle(request);
	if(connection != NULL) WinHttpCloseHandle(connection);
	if(session != NULL) WinHttpCloseHandle(session);
	return ok;
}

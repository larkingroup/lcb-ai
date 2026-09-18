#include "lcb.h"
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	Conversation c = {0};
	char *request, *answer = NULL, error[256], *end;
	long port;
	int ok;
	if(argc != 3) {
		fprintf(stderr, "usage: lcb-cli PORT PROMPT\n");
		return 2;
	}
	port = strtol(argv[1], &end, 10);
	if(*end || port < 1 || port > 65535)
		return 2;
	request = conversation_request(&c, &lcb_modules[0], argv[2]);
	if(request == NULL)
		return 2;
	ok = lcb_local_engine.complete((unsigned short)port, request, &answer, error, sizeof(error));
	if(ok) printf("%s\n", answer);
	else fprintf(stderr, "%s\n", error);
	free(answer);
	free(request);
	return ok ? 0 : 1;
}

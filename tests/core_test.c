#include "lti.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
	Conversation c = {0};
	char *request, *answer = NULL, *huge;
	cJSON *root, *messages, *content;
	const char *valid = "{\"choices\":[{\"message\":{\"content\":\"hello \\u263a\"}}]}";
	const char *bad[] = {"", "{}", "{", "{\"choices\":[]}",
	    "{\"choices\":[{\"message\":{\"content\":null}}]}",
	    "{\"choices\":[{\"message\":{\"content\":\"\"}}]}",
	    "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]} junk"};
	size_t i;
	assert(conversation_add(&c, "user", "first"));
	assert(conversation_add(&c, "assistant", "second"));
	request = conversation_request(&c, &lti_modules[0], "quote \" slash \\ newline\n");
	assert(request != NULL);
	root = cJSON_Parse(request); assert(root != NULL);
	messages = cJSON_GetObjectItem(root, "messages");
	assert(cJSON_GetArraySize(messages) == 4);
	content = cJSON_GetObjectItem(cJSON_GetArrayItem(messages, 3), "content");
	assert(strcmp(content->valuestring, "quote \" slash \\ newline\n") == 0);
	cJSON_Delete(root); free(request);
	assert(response_parse(valid, strlen(valid), &answer));
	assert(strcmp(answer, "hello \xe2\x98\xba") == 0); free(answer);
	for(i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		assert(!response_parse(bad[i], strlen(bad[i]), &answer));
	assert(!response_parse("{}\0junk", 7, &answer));
	huge = malloc(LtiMaxPrompt + 2); assert(huge != NULL);
	memset(huge, 'x', LtiMaxPrompt + 1); huge[LtiMaxPrompt + 1] = 0;
	assert(conversation_request(&c, &lti_modules[0], huge) == NULL);
	free(huge);
	while(c.count < LtiMaxMessages) assert(conversation_add(&c, "user", "x"));
	assert(!conversation_add(&c, "user", "x"));
	assert(conversation_request(&c, &lti_modules[0], "x") == NULL);
	conversation_clear(&c);
	assert(c.count == 0 && c.bytes == 0);
	puts("Core tests passed: escaping, history, Unicode, malformed JSON, bounds.");
	return 0;
}

#include "lti.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

const Module lti_modules[] = {
	{"General", "You are a helpful local assistant. Answer clearly and concisely."},
	{"Writer", "You are a local writing assistant. Help draft and revise clear prose. Preserve the user's meaning."},
	{"Programmer", "You are a local programming assistant. Prefer small, readable programs. Explain assumptions and errors. You cannot run commands or access files."}
};
const size_t lti_module_count = sizeof(lti_modules) / sizeof(lti_modules[0]);

static char *
copytext(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if(p != NULL)
		memcpy(p, s, n);
	return p;
}

void
conversation_clear(Conversation *c)
{
	size_t i;
	for(i = 0; i < c->count; i++)
		free(c->messages[i].text);
	memset(c, 0, sizeof(*c));
}

int
conversation_add(Conversation *c, const char *role, const char *text)
{
	size_t n = strlen(text);
	char *p;
	if(c->count >= LtiMaxMessages || n > LtiMaxReply ||
	    n > LtiMaxHistory - c->bytes)
		return 0;
	p = copytext(text);
	if(p == NULL)
		return 0;
	c->messages[c->count].role = role;
	c->messages[c->count++].text = p;
	c->bytes += n;
	return 1;
}

static int
addmessage(cJSON *messages, const char *role, const char *text)
{
	cJSON *m = cJSON_CreateObject();
	if(m == NULL)
		return 0;
	if(cJSON_AddStringToObject(m, "role", role) == NULL ||
	    cJSON_AddStringToObject(m, "content", text) == NULL ||
	    !cJSON_AddItemToArray(messages, m)) {
		cJSON_Delete(m);
		return 0;
	}
	return 1;
}

char *
conversation_request(const Conversation *c, const Module *module, const char *prompt)
{
	cJSON *root, *messages, *settings;
	char *result = NULL;
	size_t i, n = strlen(prompt);
	/* Reserve room for both halves of the next turn. Never silently evict history. */
	if(n == 0 || n > LtiMaxPrompt || c->count > LtiMaxMessages - 2 ||
	    c->bytes + n + LtiMaxReply > LtiMaxHistory)
		return NULL;
	root = cJSON_CreateObject();
	if(root == NULL)
		return NULL;
	messages = cJSON_AddArrayToObject(root, "messages");
	settings = cJSON_AddObjectToObject(root, "chat_template_kwargs");
	if(messages == NULL || settings == NULL ||
	    cJSON_AddBoolToObject(settings, "enable_thinking", 0) == NULL ||
	    cJSON_AddStringToObject(root, "model", "local") == NULL ||
	    cJSON_AddBoolToObject(root, "stream", 0) == NULL ||
	    cJSON_AddNumberToObject(root, "max_tokens", 1024) == NULL ||
	    !addmessage(messages, "system", module->instruction))
		goto done;
	for(i = 0; i < c->count; i++)
		if(!addmessage(messages, c->messages[i].role, c->messages[i].text))
			goto done;
	if(addmessage(messages, "user", prompt))
		result = cJSON_PrintUnformatted(root);
done:
	cJSON_Delete(root);
	return result;
}

int
response_parse(const char *wire, size_t length, char **answer)
{
	cJSON *root, *choices, *first, *message, *content;
	const char *end = NULL;
	*answer = NULL;
	if(length == 0 || length > LtiMaxWire || memchr(wire, 0, length) != NULL)
		return 0;
	root = cJSON_ParseWithLengthOpts(wire, length, &end, 0);
	if(root == NULL)
		return 0;
	while(end < wire + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
		end++;
	choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
	first = cJSON_GetArrayItem(choices, 0);
	message = cJSON_GetObjectItemCaseSensitive(first, "message");
	content = cJSON_GetObjectItemCaseSensitive(message, "content");
	if(end == wire + length && cJSON_IsArray(choices) &&
	    cJSON_IsObject(message) && cJSON_IsString(content) &&
	    strlen(content->valuestring) > 0 && strlen(content->valuestring) <= LtiMaxReply)
		*answer = copytext(content->valuestring);
	cJSON_Delete(root);
	return *answer != NULL;
}

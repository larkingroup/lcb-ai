#include "lcb.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

Generation generation_defaults(void)
{
    Generation g={1024,0.7,0.95,4096,ThinkingAuto,1.0,0.0};
    return g;
}

int generation_valid(const Generation *g)
{
    return g && g->max_tokens>=1 && g->max_tokens<=16384 &&
        g->temperature>=0 && g->temperature<=2 && g->top_p>0 && g->top_p<=1 &&
        g->context_tokens>=512 && g->context_tokens<=1048576 &&
        g->thinking>=ThinkingAuto && g->thinking<=ThinkingOn &&
        g->repeat_penalty>=1 && g->repeat_penalty<=2 &&
        g->dry_multiplier>=0 && g->dry_multiplier<=4;
}

Conversation conversation_suffix(const Conversation *c, size_t first)
{
    Conversation view={0};
    size_t i;
    if(first>c->count) return view;
    view.messages=c->messages ? c->messages+first : NULL;
    view.count=c->count-first;
    for(i=first;i<c->count;i++) view.bytes+=strlen(c->messages[i].text);
    return view;
}

const Module lcb_modules[] = {
	{"General", "You are a helpful local assistant. Answer clearly and concisely."},
	{"Writer", "You are a local writing assistant. Help draft and revise clear prose. Preserve the user's meaning."},
	{"Programmer", "You are a local programming assistant. Prefer small, readable programs. Explain assumptions and errors. You cannot run commands or access files."}
};
const size_t lcb_module_count = sizeof(lcb_modules) / sizeof(lcb_modules[0]);

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
	free(c->messages);
	memset(c, 0, sizeof(*c));
}

int
conversation_add(Conversation *c, const char *role, const char *text)
{
	size_t n = strlen(text);
	char *p;
	if(n > LcbMaxReply || n > SIZE_MAX - c->bytes)
		return 0;
	if(c->count==c->capacity) {
		size_t capacity=c->capacity ? c->capacity*2 : 32;
		Message *messages;
		if(capacity<c->capacity || capacity>SIZE_MAX/sizeof(*messages)) return 0;
		messages=realloc(c->messages,capacity*sizeof(*messages));
		if(!messages) return 0;
		c->messages=messages; c->capacity=capacity;
	}
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

static char *
request_with_settings(const Conversation *c, const Module *module, const char *prompt, const Generation *g)
{
	cJSON *root, *messages, *settings;
	char *result = NULL;
	size_t i, n = strlen(prompt);
	/* Wire limits apply to a request, never to the saved conversation. */
	if(n == 0 || n > LcbMaxPrompt || c->bytes > LcbMaxWire || c->count%2)
		return NULL;
	root = cJSON_CreateObject();
	if(root == NULL)
		return NULL;
	messages = cJSON_AddArrayToObject(root, "messages");
	if(g && g->thinking!=ThinkingAuto) {
		settings=cJSON_AddObjectToObject(root,"chat_template_kwargs");
		if(!settings || !cJSON_AddBoolToObject(settings,"enable_thinking",g->thinking==ThinkingOn)) goto done;
	}
	if(messages == NULL ||
	    cJSON_AddStringToObject(root, "model", "local") == NULL ||
	    cJSON_AddBoolToObject(root, "stream", g != NULL) == NULL ||
	    cJSON_AddNumberToObject(root, "max_tokens", g ? g->max_tokens : 1024) == NULL ||
	    !addmessage(messages, "system", module->instruction))
		goto done;
	for(i = 0; i < c->count; i++)
		if(!addmessage(messages, c->messages[i].role, c->messages[i].text))
			goto done;
	if(g && (!cJSON_AddNumberToObject(root, "temperature", g->temperature) ||
	    !cJSON_AddNumberToObject(root, "top_p", g->top_p) ||
	    !cJSON_AddNumberToObject(root, "repeat_penalty", g->repeat_penalty) ||
	    !cJSON_AddNumberToObject(root, "dry_multiplier", g->dry_multiplier))) goto done;
	if(g) {
		settings=cJSON_AddObjectToObject(root,"stream_options");
		if(!settings || !cJSON_AddBoolToObject(settings,"include_usage",1)) goto done;
	}
	if(addmessage(messages, "user", prompt))
		result = cJSON_PrintUnformatted(root);
	if(result && strlen(result)>LcbMaxWire) { free(result); result=NULL; }
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
	if(length == 0 || length > LcbMaxWire || memchr(wire, 0, length) != NULL)
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
	    strlen(content->valuestring) > 0 && strlen(content->valuestring) <= LcbMaxReply)
		*answer = copytext(content->valuestring);
	cJSON_Delete(root);
	return *answer != NULL;
}

char *conversation_request(const Conversation *c, const Module *module, const char *prompt)
{
    return request_with_settings(c,module,prompt,NULL);
}
char *conversation_generate(const Conversation *c, const Module *module, const char *prompt, const Generation *g)
{
    if(!generation_valid(g)) return NULL;
    return request_with_settings(c,module,prompt,g);
}
static int stream_line(StreamReply *s)
{
    cJSON *root, *choices, *delta, *content, *usage, *tokens, *reason;
    const char *data=s->line;
    size_t n;
    s->line[s->line_used]=0;
    if(s->line_used && s->line[s->line_used-1]=='\r') s->line[--s->line_used]=0;
    if(strncmp(data,"data:",5)) return 1;
    data+=5; if(*data==' ') data++;
    if(!strcmp(data,"[DONE]")) { s->done=1; return 1; }
    root=cJSON_ParseWithLengthOpts(data,strlen(data)+1,NULL,1);
    if(!root || cJSON_GetObjectItemCaseSensitive(root,"error")) { cJSON_Delete(root); return 0; }
    choices=cJSON_GetObjectItemCaseSensitive(root,"choices");
    if(!cJSON_IsArray(choices)) { cJSON_Delete(root); return 0; }
    delta=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(choices,0),"delta");
    reason=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(choices,0),"finish_reason");
    if(cJSON_IsString(reason)) s->finish=!strcmp(reason->valuestring,"stop")?FinishStop:
        !strcmp(reason->valuestring,"length")?FinishLength:FinishOther;
    reason=cJSON_GetObjectItemCaseSensitive(delta,"reasoning_content");
    if(cJSON_IsString(reason) && reason->valuestring[0]) s->saw_reasoning=1;
    content=cJSON_GetObjectItemCaseSensitive(delta,"content");
    if(content && !cJSON_IsNull(content) && !cJSON_IsString(content)) { cJSON_Delete(root); return 0; }
    if(cJSON_IsString(content)) {
        n=strlen(content->valuestring);
        if(n>LcbMaxReply-s->used) { cJSON_Delete(root); return 0; }
        memcpy(s->text+s->used,content->valuestring,n+1); s->used+=n;
    }
    usage=cJSON_GetObjectItemCaseSensitive(root,"usage");
    tokens=cJSON_GetObjectItemCaseSensitive(usage,"completion_tokens");
    if(cJSON_IsNumber(tokens) && tokens->valueint>0) s->tokens=tokens->valueint;
    tokens=cJSON_GetObjectItemCaseSensitive(usage,"prompt_tokens");
    if(cJSON_IsNumber(tokens) && tokens->valueint>0) s->prompt_tokens=tokens->valueint;
    cJSON_Delete(root); return 1;
}
int stream_feed(StreamReply *s, const char *data, size_t size)
{
    size_t i;
    if(s->failed || size>LcbMaxWire-s->wire) { s->failed=1; return 0; }
    s->wire+=size;
    for(i=0;i<size && !s->done;i++) {
        if(data[i]==0) { s->failed=1; return 0; }
        if(data[i]=='\n') {
            if(!stream_line(s)) { s->failed=1; return 0; }
            s->line_used=0;
        } else {
            if(s->line_used>=sizeof(s->line)-1) { s->failed=1; return 0; }
            s->line[s->line_used++]=data[i];
        }
    }
    return 1;
}

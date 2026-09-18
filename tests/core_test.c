#include "lcb.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void streaming(void)
{
    const char *wire="data: {\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}\r\n\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hello \\u263a\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":100,\"completion_tokens\":8}}\n\n"
        "data: [DONE]\n\n";
    StreamReply *reply=calloc(1,sizeof(*reply));
    Generation settings={256,0.25,0.85,4096,ThinkingAuto,1,0};
    Conversation c={0};
    char *request;
    cJSON *json;
    size_t i,fragment;
    assert(reply);
    for(fragment=1;fragment<31;fragment++) {
        memset(reply,0,sizeof(*reply));
        for(i=0;i<strlen(wire);i+=fragment) {
            size_t n=strlen(wire)-i; if(n>fragment) n=fragment;
            assert(stream_feed(reply,wire+i,n));
        }
        assert(reply->done && !strcmp(reply->text,"Hello \xe2\x98\xba world"));
        assert(reply->finish==FinishLength && reply->prompt_tokens==100 && reply->tokens==8);
    }
    memset(reply,0,sizeof(*reply));
    assert(!stream_feed(reply,"data: invalid\n",14));
    memset(reply,0,sizeof(*reply));
    assert(!stream_feed(reply,"data: {}\n",9));
    memset(reply,0,sizeof(*reply));
    reply->used=LcbMaxReply;
    assert(!stream_feed(reply,wire,strlen(wire)));
    free(reply);
    request=conversation_generate(&c,&lcb_modules[0],"hello",&settings); assert(request);
    json=cJSON_Parse(request); assert(json);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(json,"stream")));
    assert(cJSON_GetObjectItem(json,"max_tokens")->valueint==256);
    assert(cJSON_GetObjectItem(json,"temperature")->valuedouble==0.25);
    assert(cJSON_GetObjectItem(json,"top_p")->valuedouble==0.85);
    assert(!cJSON_GetObjectItem(json,"chat_template_kwargs"));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(json,"stream_options"),"include_usage")));
    free(request); cJSON_Delete(json);
    settings.max_tokens=0; assert(!conversation_generate(&c,&lcb_modules[0],"hello",&settings));
}

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
	streaming();
	assert(conversation_add(&c, "user", "first"));
	assert(conversation_add(&c, "assistant", "second"));
	request = conversation_request(&c, &lcb_modules[0], "quote \" slash \\ newline\n");
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
	huge = malloc(LcbMaxPrompt + 2); assert(huge != NULL);
	memset(huge, 'x', LcbMaxPrompt + 1); huge[LcbMaxPrompt + 1] = 0;
	assert(conversation_request(&c, &lcb_modules[0], huge) == NULL);
	free(huge);
	for(i=0;i<5000;i++) {
        assert(conversation_add(&c,"user","A saved question"));
        assert(conversation_add(&c,"assistant","A saved answer"));
    }
    assert(c.count==10002);
    request=conversation_request(&c,&lcb_modules[0],"Still going");
    assert(request); free(request);
    { Conversation view=conversation_suffix(&c,c.count-4);
      assert(view.count==4 && view.messages==c.messages+c.count-4);
      request=conversation_request(&view,&lcb_modules[0],"Next"); assert(request); free(request);
      assert(c.count==10002);
    }
	conversation_clear(&c);
	assert(c.count == 0 && c.bytes == 0);
	puts("Core tests passed: escaping, history, Unicode, malformed JSON, bounds.");
	return 0;
}

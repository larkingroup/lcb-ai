#ifndef LTI_H
#define LTI_H

#include <stddef.h>

enum {
	LtiMaxMessages = 32,
	LtiMaxPrompt = 16384,
	LtiMaxReply = 65536,
	LtiMaxWire = 1048576,
	LtiMaxHistory = 131072
};

typedef struct Message Message;
typedef struct Conversation Conversation;
typedef struct Module Module;
typedef struct Engine Engine;

struct Message {
	const char *role;
	char *text;
};

struct Conversation {
	Message messages[LtiMaxMessages];
	size_t count;
	size_t bytes;
};

struct Module {
	const char *name;
	const char *instruction;
};

/* Engine owns its transport. Caller owns the returned UTF-8 answer. */
struct Engine {
	const char *name;
	int (*complete)(unsigned short port, const char *request,
	    char **answer, char *error, size_t capacity);
};

extern const Module lti_modules[];
extern const size_t lti_module_count;
extern const Engine lti_local_engine;

void conversation_clear(Conversation *c);
int conversation_add(Conversation *c, const char *role, const char *text);
char *conversation_request(const Conversation *c, const Module *module,
    const char *prompt);
int response_parse(const char *wire, size_t length, char **answer);
int local_complete(unsigned short port, const char *request,
    char **answer, char *error, size_t capacity);

#endif

#ifndef LTS_H
#define LTS_H

#include <stddef.h>

enum {
	LtsMaxMessages = 32,
	LtsMaxPrompt = 16384,
	LtsMaxReply = 65536,
	LtsMaxWire = 1048576,
	LtsMaxHistory = 131072
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
	Message messages[LtsMaxMessages];
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

extern const Module lts_modules[];
extern const size_t lts_module_count;
extern const Engine lts_local_engine;

typedef struct Generation { int max_tokens; double temperature, top_p; } Generation;
typedef struct StreamReply {
	char line[LtsMaxReply + 1024], text[LtsMaxReply + 1];
	size_t line_used, used, wire;
	int done, failed, tokens;
} StreamReply;
/* Feed arbitrary network fragments; UTF-8 and JSON may span multiple fragments. */
int stream_feed(StreamReply *s, const char *data, size_t size);
char *conversation_generate(const Conversation *c, const Module *module,
    const char *prompt, const Generation *settings);

void conversation_clear(Conversation *c);
int conversation_add(Conversation *c, const char *role, const char *text);
char *conversation_request(const Conversation *c, const Module *module,
    const char *prompt);
int response_parse(const char *wire, size_t length, char **answer);
int local_complete(unsigned short port, const char *request,
    char **answer, char *error, size_t capacity);

#endif

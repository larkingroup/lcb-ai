#ifndef LCB_H
#define LCB_H

#include <stddef.h>

enum {
	LcbMaxPrompt = 16384,
	LcbMaxReply = 65536,
	LcbMaxWire = 1048576
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
	Message *messages;
	size_t count;
	size_t capacity;
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

extern const Module lcb_modules[];
extern const size_t lcb_module_count;
extern const Engine lcb_local_engine;

enum { ThinkingAuto, ThinkingOff, ThinkingOn };
typedef struct Generation {
	int max_tokens;
	double temperature, top_p;
	int context_tokens, thinking;
	double repeat_penalty, dry_multiplier;
} Generation;
Generation generation_defaults(void);
int generation_valid(const Generation *g);
/* A borrowed suffix of a saved conversation; never free it. */
Conversation conversation_suffix(const Conversation *c, size_t first);
typedef struct StreamReply {
	char line[LcbMaxReply + 1024], text[LcbMaxReply + 1];
	size_t line_used, used, wire;
	int done, failed, tokens, prompt_tokens, finish, saw_reasoning;
} StreamReply;
enum { FinishUnknown, FinishStop, FinishLength, FinishOther };
typedef struct ReplyReport { int prompt_tokens, tokens, finish, saw_reasoning; } ReplyReport;
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

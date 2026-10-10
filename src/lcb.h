#ifndef LCB_H
#define LCB_H

#include <stddef.h>

enum {
	LcbMaxPrompt = 16384,
	LcbMaxReply = 65536,
	LcbMaxWire = 1048576,
    LcbMaxStreamWire = 16 * 1048576
};

typedef struct Message Message;
typedef struct Conversation Conversation;
typedef struct Module Module;
typedef struct Engine Engine;

enum { AnswerUnknown, AnswerComplete, AnswerStopped, AnswerLength, AnswerError, AnswerOther };
const char *answer_status_name(int status);
int answer_status(int result, int finish);

struct Message {
	const char *role;
	char *text;
	int status;
	char error[256];
    char *reasoning; /* Saved locally; excluded from request messages. */
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
enum { GenResponse, GenTemperature, GenTopP, GenContext, GenThinking, GenRepeat,
    GenDry, GenTopK, GenMinP, GenPresence, GenCount };
typedef struct Generation {
	int max_tokens;
	double temperature, top_p;
	int context_tokens, thinking;
	double repeat_penalty, dry_multiplier;
	int top_k;
	double min_p, presence_penalty;
} Generation;
Generation generation_defaults(void);
int generation_valid(const Generation *g);
double generation_value(const Generation *g, int field);
int generation_set(Generation *g, int field, double value);
/* A borrowed suffix of a saved conversation; never free it. */
Conversation conversation_suffix(const Conversation *c, size_t first);
typedef struct StreamReply {
	char line[LcbMaxReply + 1024], text[LcbMaxReply + 1];
	size_t line_used, used, wire;
	int done, failed, tokens, prompt_tokens, finish, saw_reasoning;
    char reasoning[LcbMaxReply + 1];
    size_t reasoning_used;
    int progress_total, progress_processed, progress_cached;
    double progress_ms;
} StreamReply;
enum { FinishUnknown, FinishStop, FinishLength, FinishOther };
typedef struct ReplyReport { int prompt_tokens, tokens, finish, saw_reasoning; } ReplyReport;
/* Feed arbitrary network fragments; UTF-8 and JSON may span multiple fragments. */
int stream_feed(StreamReply *s, const char *data, size_t size);
char *conversation_generate(const Conversation *c, const Module *module,
    const char *prompt, const Generation *settings);

char *conversation_generate_capable(const Conversation *c, const Module *module,
    const char *prompt, const Generation *settings, int supports_effort);
const char *reasoning_settings_error(const Generation *settings, int supports_effort);
enum { CountFailed, CountOk, CountTooLarge };
typedef int (*PromptCounter)(const char *request, void *context, int *tokens,
    char *error, size_t capacity);
typedef struct FitBudget { int prompt_tokens; size_t omitted_messages; } FitBudget;
/* Counts exact candidates through the platform adapter. Never modifies history. */
int conversation_fit(const Conversation *c, const Module *module, const char *prompt,
    const Generation *settings, int context_tokens, int supports_effort,
    PromptCounter counter, void *counter_context, char **request, FitBudget *budget,
    char *error, size_t capacity);
void conversation_clear(Conversation *c);
int conversation_add(Conversation *c, const char *role, const char *text);
char *conversation_request(const Conversation *c, const Module *module,
    const char *prompt);
int response_parse(const char *wire, size_t length, char **answer);
int local_complete(unsigned short port, const char *request,
    char **answer, char *error, size_t capacity);

#endif

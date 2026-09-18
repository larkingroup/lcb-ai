#ifndef CONTEXT_WIN_H
#define CONTEXT_WIN_H
#include "stream_win.h"
typedef struct ContextBudget {
    int context_tokens, prompt_tokens, response_tokens, thinking_supported;
    size_t omitted_messages;
    char build[96];
} ContextBudget;
/* Builds a request without changing saved history. All network work is cancellable. */
int local_prepare(unsigned short port, const wchar_t *expected_model,
    const Conversation *conversation, const Module *module, const char *prompt,
    const Generation *settings, HANDLE cancel, char **request, ContextBudget *budget,
    char *error, size_t capacity);
#endif

#ifndef LCB_STREAM_WIN_H
#define LCB_STREAM_WIN_H
#include <windows.h>
#include "lcb.h"
typedef void (*StreamUpdate)(const char *text, void *context);
enum { LocalJsonFailed, LocalJsonOk, LocalJsonTooLarge };
/* Cancellable, bounded local JSON requests. Only LocalJsonOk supplies an answer. */
int local_json(unsigned short port, const wchar_t *path, const char *body,
    HANDLE cancel, char **answer, char *error, size_t capacity);
/* 1 complete, 2 cancelled, 0 failed. All results may return partial answer text;
 * the caller owns it and must check the result before treating it as complete. */
int local_stream(unsigned short port, const char *body, HANDLE cancel,
    StreamUpdate update, void *context, char **answer, char *error, size_t capacity);
int local_stream_report(unsigned short port, const char *body, HANDLE cancel,
    StreamUpdate update, void *context, char **answer, ReplyReport *report, char *error, size_t capacity);
#endif

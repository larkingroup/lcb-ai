#ifndef LTS_STREAM_WIN_H
#define LTS_STREAM_WIN_H
#include <windows.h>
#include "lts.h"
typedef void (*StreamUpdate)(const char *text, void *context);
/* 1 complete, 2 cancelled (possibly with partial text), 0 failed. */
int local_stream(unsigned short port, const char *body, HANDLE cancel,
    StreamUpdate update, void *context, char **answer, char *error, size_t capacity);
#endif

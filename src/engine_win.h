#ifndef ENGINE_WIN_H
#define ENGINE_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { EngineOffline, EngineLoading, EngineReady, EngineOther };
typedef struct EngineProcess EngineProcess;
struct EngineProcess { HANDLE process, job; };

int engine_probe(unsigned short port);
int engine_start(EngineProcess *p, const wchar_t *exe, const wchar_t *model,
    unsigned short port, wchar_t *error, size_t capacity);
int engine_start_context(EngineProcess *p, const wchar_t *exe, const wchar_t *model,
    unsigned short port, int context, wchar_t *error, size_t capacity);
void engine_stop(EngineProcess *p);
int engine_exited(EngineProcess *p);
#endif

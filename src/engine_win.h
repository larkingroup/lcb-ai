#ifndef ENGINE_WIN_H
#define ENGINE_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { EngineOffline, EngineLoading, EngineReady, EngineOther };
typedef struct EngineProcess EngineProcess;
struct EngineProcess { HANDLE process, job; wchar_t logpath[MAX_PATH]; DWORD exitcode; };

int engine_probe(unsigned short port);
int engine_start(EngineProcess *p, const wchar_t *exe, const wchar_t *model,
    unsigned short port, wchar_t *error, size_t capacity);
int engine_start_context(EngineProcess *p, const wchar_t *exe, const wchar_t *model,
    unsigned short port, int context, wchar_t *error, size_t capacity);
int engine_start_arguments(EngineProcess *p, const wchar_t *exe, const wchar_t *model,
    unsigned short port, int context, const wchar_t *extra, wchar_t *error, size_t capacity);
int engine_arguments_valid(const wchar_t *extra);
void engine_stop(EngineProcess *p);
int engine_exited(EngineProcess *p);
void engine_read_log(const EngineProcess *p, wchar_t *text, size_t capacity);
#endif

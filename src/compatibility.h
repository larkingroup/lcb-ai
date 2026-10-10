#ifndef LCB_COMPATIBILITY_H
#define LCB_COMPATIBILITY_H
#include <wchar.h>

enum { RuntimeDefault, RuntimePTQ, RuntimePQ, RuntimeHadamard };
const wchar_t *runtime_requirement_text(int requirement);
const wchar_t *runtime_setup_url(void);
const wchar_t *engine_failure_advice(const wchar_t *log);
#endif

#ifndef MODEL_SETTINGS_WIN_H
#define MODEL_SETTINGS_WIN_H
#include <wchar.h>
#include "lcb.h"
void model_settings_load(const wchar_t *config, const wchar_t *model, Generation *g);
int model_settings_save(const wchar_t *config, const wchar_t *model, const Generation *g);
#endif

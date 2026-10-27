#ifndef MODEL_SETTINGS_WIN_H
#define MODEL_SETTINGS_WIN_H
#include <wchar.h>
#include "lcb.h"
void model_settings_load(const wchar_t *config, const wchar_t *model, Generation *g);
int model_settings_save(const wchar_t *config, const wchar_t *model, const Generation *g);
int model_settings_save_field(const wchar_t *config, const wchar_t *model, const Generation *g, int field);
/* The model's media projector: the saved choice (empty = text only), else the
 * matching mmproj-*.gguf beside the model, else empty. */
void model_projector_load(const wchar_t *config, const wchar_t *model, wchar_t *projector /* MAX_PATH */);
int model_projector_save(const wchar_t *config, const wchar_t *model, const wchar_t *projector);
#endif

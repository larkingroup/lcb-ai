#ifndef LIBRARY_WIN_H
#define LIBRARY_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "lcb.h"

enum { LibraryMax = 512 };
typedef struct ModelInfo {
	wchar_t path[MAX_PATH], name[256], architecture[80], size[40];
	wchar_t identity[256], basename[256], base_model[256];
	Generation sampling;
	unsigned sampling_fields;
	int thinking_switch;
	uint32_t base_count;
	uint64_t bytes;
	uint32_t filetype;
	int projector;
} ModelInfo;
typedef struct Library {
	ModelInfo models[LibraryMax];
	unsigned count, skipped, folders;
	int limited;
} Library;
int model_read(const wchar_t *path, ModelInfo *model);
int model_compare(const ModelInfo *a, const ModelInfo *b, int column);
const wchar_t *model_quant(uint32_t filetype);
void library_scan(Library *library, const wchar_t *folder, int recursive,
    volatile LONG *cancel);
#endif

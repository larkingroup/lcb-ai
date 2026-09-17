#ifndef LIBRARY_WIN_H
#define LIBRARY_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

enum { LibraryMax = 512 };
typedef struct ModelInfo {
	wchar_t path[MAX_PATH], name[256], architecture[80], size[40];
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
const wchar_t *model_quant(uint32_t filetype);
void library_scan(Library *library, const wchar_t *folder, int recursive,
    volatile LONG *cancel);
#endif

#ifndef ATTACHMENTS_WIN_H
#define ATTACHMENTS_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "cJSON.h"
#include "lcb.h"

/* Saved attachments, in the Linux build's format: each file is copied once to
 * <root>\attachments\<sha256>.<ext>, and a message keeps a JSON array of
 * {"file","name","kind","size"} items. Lists are UTF-8 JSON text; NULL is empty. */
enum { AttachMax = 8 };

int attach_valid(const cJSON *items);
int attach_count(const char *list);
/* Images, audio and video; saved text files need no projector. */
int attach_media_count(const char *list);
/* True for a file that Attach inserts into the message as text. */
int attach_is_text(const wchar_t *path);
/* The file dialog filter for media files. */
const wchar_t *attach_filter(void);
/* Copies a media file into the store and returns a new list with it appended. */
char *attach_import(const wchar_t *root, const char *list, const wchar_t *path,
    wchar_t *error, size_t capacity);
/* A new list without one item, or NULL when none remain. */
char *attach_remove(const char *list, int index);
/* The item's file name for display, as UTF-16. */
int attach_name(const char *list, int index, wchar_t *name, size_t capacity);
/* "a.png, b.wav" for display. */
void attach_summary(const char *list, wchar_t *text, size_t capacity);
/* Replaces each user turn's content in a generated request with text and
 * media parts. Earlier media the engine cannot read is skipped; the new
 * message's media must be supported. Frees and replaces *request. */
int attach_request(const wchar_t *root, const Conversation *c, const char *current,
    const cJSON *modalities, HANDLE cancel, char **request, char *error, size_t capacity);
#endif

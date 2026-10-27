#ifndef STORE_WIN_H
#define STORE_WIN_H
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "lcb.h"

enum { StoreName = 241, StoreWorkspaces = 64 };
typedef struct Workspace Workspace;
typedef struct Chat Chat;
typedef struct ChatInfo ChatInfo;
typedef struct Store Store;
struct Workspace { char id[33], name[StoreName], prompt[LcbMaxPrompt+1]; };
/* updated: Unix seconds of the last exchange; pinned and renamed are flags. */
struct ChatInfo { char id[33], workspace[33], title[StoreName]; size_t turns;
	long long updated; int pinned, renamed; };

/* draftattachments: files attached to the unsent draft, as a JSON array or NULL. */
struct Chat { ChatInfo info; Conversation conversation; char draft[LcbMaxPrompt+1]; char *draftattachments; };
struct Store {
	wchar_t root[MAX_PATH], error[512];
	HANDLE lock;
	Workspace *workspaces;
	ChatInfo *chats;
	size_t nworkspaces, nchats, capchats, skipped;
};

int store_open(Store *s, const wchar_t *root);
void store_close(Store *s);
int store_workspace(Store *s, Workspace *w);
int store_save(Store *s, Chat *chat);
int store_load(Store *s, const char *id, Chat *chat);
/* Removes an indexed chat. The index changes only after the file is deleted. */
int store_delete(Store *s, const char *id);
int store_new(Store *s, const char *workspace, Chat *chat);
/* Copies exchanges before the zero-based turn. Destination can equal source;
 * neither is changed unless the new chat is saved successfully. */
int store_branch(Store *s, const Chat *source, size_t turn, const char *prompt,
    const char *action, Chat *destination);
Workspace *store_find_workspace(Store *s, const char *id);
long long store_now(void);

void chat_clear(Chat *chat);
#endif

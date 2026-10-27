#ifndef LCB_SHORTCUTS_H
#define LCB_SHORTCUTS_H
/* Keyboard shortcuts shared by the Windows and Linux interfaces. Help >
 * Keyboard shortcuts lists this table, so both stay in step with it. */
typedef struct LcbShortcut { const char *group, *action, *keys; } LcbShortcut;

static const LcbShortcut lcb_shortcuts[] = {
	{"Chats", "New chat", "Ctrl+N"},
	{"Chats", "Close chat", "Ctrl+W"},
	{"Chats", "Rename chat", "F2"},
	{"Chats", "Delete chat (in the chat list)", "Del"},
	{"Chats", "Find chat", "Ctrl+F or Ctrl+K"},
	{"Chats", "Open the first search result", "Enter"},
	{"Chats", "Previous chat", "Ctrl+PgUp or Ctrl+Shift+Tab"},
	{"Chats", "Next chat", "Ctrl+PgDn or Ctrl+Tab"},
	{"Message", "Send", "Ctrl+Enter"},
	{"Message", "Stop response", "Esc"},
	{"Message", "Retry", "Ctrl+R"},
	{"Message", "Edit and resend", "Ctrl+E"},
	{"Message", "Copy", "Ctrl+C"},
	{"Message", "Select all", "Ctrl+A"},
	{"Model", "Load model", "F5"},
	{"Model", "Open model file", "Ctrl+O"},
	{"Model", "Properties", "Alt+Enter"},
	{"Model", "Engine options", "Ctrl+,"},
	{"Window", "Chats sidebar", "F9"},
	{"Window", "Output", "F12"},
	{"Window", "Keyboard shortcuts", "F1"},
	{"Window", "Quit", "Ctrl+Q"},
};
enum { LcbShortcutCount = sizeof(lcb_shortcuts) / sizeof(lcb_shortcuts[0]) };
#endif

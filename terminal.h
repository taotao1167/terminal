#ifndef __TERMINAL_H__
#define __TERMINAL_H__

#include <stdint.h>
#include <stdarg.h>
#if defined(_WIN32)
#define ssize_t size_t
#endif
#include "tt_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TERM_FGCOLOR_DEFAULT         0
#define TERM_FGCOLOR_BLACK           30
#define TERM_FGCOLOR_RED             31
#define TERM_FGCOLOR_GREEN           32
#define TERM_FGCOLOR_YELLOW          33
#define TERM_FGCOLOR_BLUE            34
#define TERM_FGCOLOR_MAGENTA         35
#define TERM_FGCOLOR_CYAN            36
#define TERM_FGCOLOR_WHITE           37
#define TERM_FGCOLOR_BRIGHT_BLACK    90
#define TERM_FGCOLOR_BRIGHT_RED      91
#define TERM_FGCOLOR_BRIGHT_GREEN    92
#define TERM_FGCOLOR_BRIGHT_YELLOW   93
#define TERM_FGCOLOR_BRIGHT_BLUE     94
#define TERM_FGCOLOR_BRIGHT_MAGENTA  95
#define TERM_FGCOLOR_BRIGHT_CYAN     96
#define TERM_FGCOLOR_BRIGHT_WHITE    97

#define TERM_BGCOLOR_DEFAULT         0
#define TERM_BGCOLOR_BLACK           (40 << 9)
#define TERM_BGCOLOR_RED             (41 << 9)
#define TERM_BGCOLOR_GREEN           (42 << 9)
#define TERM_BGCOLOR_YELLOW          (43 << 9)
#define TERM_BGCOLOR_BLUE            (44 << 9)
#define TERM_BGCOLOR_MAGENTA         (45 << 9)
#define TERM_BGCOLOR_CYAN            (46 << 9)
#define TERM_BGCOLOR_WHITE           (47 << 9)
#define TERM_BGCOLOR_BRIGHT_BLACK    (100 << 8)
#define TERM_BGCOLOR_BRIGHT_RED      (101 << 8)
#define TERM_BGCOLOR_BRIGHT_GREEN    (102 << 8)
#define TERM_BGCOLOR_BRIGHT_YELLOW   (103 << 8)
#define TERM_BGCOLOR_BRIGHT_BLUE     (104 << 8)
#define TERM_BGCOLOR_BRIGHT_MAGENTA  (105 << 8)
#define TERM_BGCOLOR_BRIGHT_CYAN     (106 << 8)
#define TERM_BGCOLOR_BRIGHT_WHITE    (107 << 8)

#define TERM_STYLE_BOLD              0x010000
#define TERM_STYLE_UNDERSCORE        0x020000
#define TERM_STYLE_BLINKING          0x040000
#define TERM_STYLE_INVERSE           0x080000
#define TERM_COLOR_DEFAULT           TERM_FGCOLOR_DEFAULT | TERM_BGCOLOR_DEFAULT

#define MULSEL_OPTIONAL              (1 << 0)

typedef enum TermEvent {
	E_EVENT_NONE,
	E_EVENT_COMPLETE,
	E_EVENT_HELP,
	E_EVENT_EXEC
} TermEvent;

typedef enum NodeType {
	TYPE_UNSET = 0,
	TYPE_KEY,
	TYPE_TEXT,
	TYPE_SELECT,
	TYPE_MULSEL,
} NodeType;

typedef struct TermNode TermNode;
typedef struct Terminal Terminal;

typedef void (* TermExec)(struct Terminal *term, int argc, const char **argv);
typedef void (* TermDynOptionCb)(void *userdata, char ***word, char ***help, int *num);


extern TermNode *term_root_create();

extern TermNode *term_node_child_add(TermNode *parent, NodeType type, const char *word, const char *help, TermExec exec);
extern int term_node_child_del(TermNode *parent, const char *word);
extern TermNode *term_node_select_add(TermNode *parent, const char *word, TermExec exec);
extern TermNode *term_node_mulsel_add(TermNode *parent, const char *word, uint32_t flags, TermExec exec);
extern TermNode *term_node_option_add(TermNode *selector, const char *word, const char *help);
extern int term_node_option_del(TermNode *selector, const char *word);

extern int term_node_dynamic_option(TermNode *selector, TermDynOptionCb cb_func, void *userdata);

extern void term_root_free(TermNode *root);

extern int term_create(Terminal **_term, const char *prompt, TermNode *root, const char *init_content);
extern void term_destroy(Terminal *term);

extern int term_root_set(Terminal *term, TermNode *root);
extern void term_exit(Terminal *term);
extern int term_loop(Terminal *term);
extern void term_color_set(Terminal *term, unsigned int color);
extern int term_prompt_set(Terminal *term, const char *prompt);
extern void term_prompt_color_set(Terminal *term, unsigned int color);
extern void term_userdata_set(Terminal *term, void *userdata);
extern void *term_userdata_get(Terminal *term);
extern const char *term_getline(Terminal *term, const char *prefix);
extern const char *term_password(Terminal *term, const char *prefix);
extern int term_vprintf(Terminal *term, const char *format, va_list args);
extern int term_printf(Terminal *term, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif


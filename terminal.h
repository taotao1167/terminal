#ifndef __TERMINAL_H__
#define __TERMINAL_H__

#include <stdarg.h>
#include "tt_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HISTORY_LENGTH     20 /* 0 means no limits */

#define MATCH_EMPTY        (1 << 0)
#define MATCH_PART         (1 << 1)
#define MATCH_ENTIRE       (1 << 2)
#define MATCH_ACT_EXPAND   (0x10 << 0)
#define MATCH_ACT_FORWARD  (0x10 << 1)
#define MATCH_ACT_EXEC     (0x10 << 2)

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
#define TERM_COLOR_DEFAULT          TERM_FGCOLOR_DEFAULT | TERM_BGCOLOR_DEFAULT

typedef enum E_TERM_EVENT {
	E_EVENT_NONE,
	E_EVENT_COMPLETE,
	E_EVENT_HELP,
	E_EVENT_EXEC
}TERM_EVENT;

struct ST_TERM_STRINGS {
	char *content;
	struct ST_TERM_STRINGS *prev;
	struct ST_TERM_STRINGS *next;
};
typedef struct ST_TERM_STRINGS TERM_LINES;
typedef struct ST_TERM_STRINGS TERM_HISTORY;

typedef struct ST_TERM_ARGS {
	char *content;
	int endword; /* true if command end with word instead of SPACE */
	struct ST_TERM_ARGS *prev;
	struct ST_TERM_ARGS *next;
}TERM_ARGS;

struct ST_TERM_WORDHELP {
	char *word;
	char *help;
	struct ST_TERM_WORDHELP *next;
};
typedef struct ST_TERM_WORDHELP TERM_COMPLETE;
typedef struct ST_TERM_WORDHELP TERM_HINTS;

typedef struct ST_TERMINAL TERMINAL;
struct ST_TERMINAL {
	char *prompt;
	unsigned int prompt_color;
	TT_BUFFER prefix; /* saved content for multilien " ' \ */
	TT_BUFFER line_command;
	TT_BUFFER tempbuf; /* for format output */
	/* TERM_LINES *outlines;  for paging */
	TERM_HISTORY *history;
	TERM_HISTORY *history_cursor;
	int pos; /* cursor position */
	int num; /* length of line_command */
	int mask; /* set true if need mask */
	int multiline; /* true if line command end with '\\' or found '\"' or '\'' but not close */
	int exit_flag; /* set true when need exit term_readline */
	TERM_EVENT event;
	TERM_ARGS *command_args;
	TERM_COMPLETE *complete;
	TERM_HINTS *hints;
	ssize_t (*read)(void *buf, size_t count);
	ssize_t (*write)(const void *buf, size_t count);
	void (*event_cb)(TERMINAL *term);
};

extern int term_init(TERMINAL *term, const char *prompt, void (*cb)(TERMINAL *term));
extern void term_exit(TERMINAL *term);
extern void term_free(TERMINAL *term);
extern int term_vprintf(TERMINAL *term, const char *format, va_list args);
extern int term_printf(TERMINAL *term, const char *format, ...);
extern void term_color_set(TERMINAL *term, unsigned int color);
extern int term_prompt_set(TERMINAL *term, const char *prompt);
extern void term_prompt_color_set(TERMINAL *term, unsigned int color);
extern void term_event_bind(TERMINAL *term, void (*cb)(TERMINAL *term));
extern int term_readline(TERMINAL *term);
extern int keyword(TERMINAL *term, TERM_ARGS *p_arg, const char *target, const char *word, const char *help);
extern int argument(TERMINAL *term, TERM_ARGS *p_arg, const char *word, const char *help);

#ifdef __cplusplus
}
#endif

#endif


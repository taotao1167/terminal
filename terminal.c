#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>

#if defined(_WIN32)
	#include <io.h>
	#include <conio.h>
	#include <windows.h>
	#ifndef STDIN_FILENO
		#define STDIN_FILENO _fileno(stdin)
		#define STDOUT_FILENO _fileno(stdout)
	#endif
	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
	#define read _read
	#define write _write
	#define isatty _isatty
#else /* Linux */
	#include <unistd.h>
	#include <termios.h>
	#include <fcntl.h>
	#include <signal.h>
	#include <sys/ioctl.h>
#endif	/* end of #if defined(_WIN32) */

#ifndef __TERMINAL_H__
#include "terminal.h"
#endif /* end of #ifndef __TERMINAL_H__ */

#define HISTORY_LENGTH     20
#define WALK_MAX_DEEP      32

#define MATCH_NONE         0
#define MATCH_PART         1
#define MATCH_ALL          2

#define MY_MALLOC(x) malloc((x))
#define MY_FREE(x) free((x))
#define MY_REALLOC(x, y) realloc((x), (y))
#define MY_STRDUP(x) strdup((x))

#define CTRL_FLAG (0x01000000)
#define ALT_FLAG (0x02000000)
#define KEY_CTRL(key) ((key) - 0x40)
#define KEY_ALT(key) (ALT_FLAG | (key))
#define KEY_BACKSPACE (0x08)
#define KEY_TAB (0x09)
#define KEY_LF (0x0a)
#define KEY_CR (0x0d)
#define KEY_ESC (0x1b)
#define KEY_FUN(num) ((num) << 8)
#define KEY_UP (0x10 << 8)
#define KEY_DOWN (0x11 << 8)
#define KEY_RIGHT (0x12 << 8)
#define KEY_LEFT (0x13 << 8)
#define KEY_END (0x14 << 8)
#define KEY_HOME (0x15 << 8)
#define KEY_PGUP (0x16 << 8)
#define KEY_PGDN (0x17 << 8)
#define KEY_INSERT (0x18 << 8)
#define KEY_DELETE (0x19 << 8)

typedef struct TermWordHelp {
	char *word;
	char *help;
	struct TermWordHelp *next;
} TermWordHelp ;

typedef struct TermWordHelp TermComplete;
typedef struct TermWordHelp TermHits;

struct TermNode {
	NodeType type;
	char *word;
	char *help;
	uint32_t flags; /* is MULSEL_OPTIONAL if allow TYPE_MULSEL empty */
	int option_index; /* option index in selector */
	struct TermNode *selector; /* option in select will use */
	struct TermNode *option; /* select will use */
	struct TermNode *children;
	struct TermNode *next;
	TermDynOptionCb dyn_option;
	void *dyn_option_udata;
	TermExec exec;
};

typedef struct TermArg {
	char *content;
	struct TermArg *next;
} TermArg;

struct Terminal {
    char *init_content;
    int init_content_offset;
	TermNode *root; /* a tree */
	char *prompt;
	char *default_prompt;
	unsigned int prompt_color;
	TTBuffer prefix; /* saved content for multiline " ' \ */
	TTBuffer line_command;
	TTBuffer tempbuf; /* for format output */
	int history_cnt;
	int history_cur; /* current histroy index */
	char **history; /* histroy content */
	char *line; /* term_getline or term_password will use */
	int pos; /* cursor position */
	int num; /* length of line_command */
	int mask; /* set true if need mask */
	int multiline; /* true if line command end with '\\' or found '\"' or '\'' but not close */
	int exit_flag; /* set true when need exit term_loop */
	int spacetail; /* command has space tail or not */
	TermEvent event;
	TermArg *command_args;
	TermComplete *complete;
	TermHits *hints;
	int exec_num;
	ssize_t (*read)(struct Terminal *term, void *buf, size_t count);
	ssize_t (*write)(struct Terminal *term, const void *buf, size_t count);
	void *userdata; /* set by term_prompt_userdata_set */
};

typedef struct WalkStacked {
	TermNode *node;
	TermArg *arg;
	uint64_t checked; /* checked options in TYPE_MULSEL */
	uint64_t walked; /* walked options in TYPE_MULSEL */
	int optional; /* TYPE_MULSEL is optional or not */
	char *exec_argv;
} WalkStacked;

static int isdelimiter(char ch) {
	int i = 0;
	const char *target = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

	for (i = 0; target[i] != '\0'; i++) {
		if (target[i] == ch) {
			return 1;
		}
	}
	return 0;
}

static int term_vprintf_inner(Terminal *term, const char *format, va_list args) {
	int ret = -1;

	if ((ret = tt_buffer_vprintf(&(term->tempbuf), format, args)) < 0) {
		ret = -1;
		goto func_end;
	}
	term->write(term, term->tempbuf.content, term->tempbuf.used);
	term->tempbuf.used = 0;
func_end:
	return ret;
}

static int term_printf_inner(Terminal *term, const char *format, ...) {
	int rc;
	va_list args;

	va_start(args, format);
	rc = term_vprintf_inner(term, format, args);
	va_end(args);
	return rc;
}

static int term_command_write(Terminal *term, const void *content, size_t count) {
	return tt_buffer_write(&(term->line_command), content, count);
}

static void term_free_args(Terminal *term) {
	TermArg *p_cur = NULL, *p_next = NULL;

	for (p_cur = term->command_args; p_cur != NULL; p_cur = p_next) {
		p_next = p_cur->next;
		if (p_cur->content) {
			MY_FREE(p_cur->content);
		}
		MY_FREE(p_cur);
	}
	term->command_args = NULL;
}

/* term_split_args return < 0 if need continue read content, for case multiline or quot */
static int term_split_args(Terminal *term) {
	int ret = 0;
	const char *start = NULL, *end = NULL;
	TermArg *p_new = NULL, *p_tail = NULL;
	TTBuffer command_buf, arg_buf;
	char in_quot = '\0';
	int backslash_tail = 0, eof = 0;

	term_free_args(term);
	tt_buffer_init(&command_buf);
	tt_buffer_init(&arg_buf);
	if (!term->multiline) {
		tt_buffer_empty(&(term->prefix));
	}

	if (term->multiline) { /* merge prefix and line_command */
		tt_buffer_empty(&command_buf);
		tt_buffer_write(&command_buf, term->prefix.content, term->prefix.used);
		tt_buffer_write(&command_buf, term->line_command.content, term->line_command.used);
		start = (char *)(command_buf.content);
	} else {
		start = (char *)(term->line_command.content);
	}

	while (1) { /* parse all content */
		if (in_quot == '\0') {
			for (; *start == ' '; start++); /* move to first word for lstrip */
		}
		for (end = start; ; end++) { /* parse one argument */
			if ((*end == '"' || *end == '\'') && (end == start || *(end - 1) != '\\')) { /* found '"' or '\'' and no escape(\) */
				if (in_quot == '\0') { /* is quot start */
					in_quot = *end;
					continue;
				} else if (*end == in_quot) { /* is quot end */
					in_quot = '\0';
					continue;
				}
			}
			if (*end == '\\') {
				if (*(end + 1) == '\0') { /* found '\\' at end of content, is multiline */
					backslash_tail = 1;
					start = end + 1;
					eof = 1;
					break;
				} else if (*(end + 1) == 'n') { /* escape '\n' */
					end++;
					tt_buffer_write(&arg_buf, "\n", 1);
					continue;
				} else if (*(end + 1) == ' ' || *(end + 1) == '\\' || *(end + 1) == '\'' || *(end + 1) == '"') { /* escape SPACE, BACKSPLASH and QUOT */
					end++;
					tt_buffer_write(&arg_buf, end, 1);
					continue;
				}
			}
			if (in_quot == '\0') { /* not between quot */
				if (*end != ' ' && *end != '\0') {
					tt_buffer_write(&arg_buf, end, 1);
					continue;
				}
				if (arg_buf.used > 0) {
					p_new = (TermArg *)MY_MALLOC(sizeof(TermArg));
					if (p_new == NULL) {
						goto func_end;
					}
					memset(p_new, 0x00, sizeof(TermArg));
					p_new->content = (char *)MY_STRDUP((char *)arg_buf.content);
					if (p_new->content == NULL) {
						goto func_end;
					}
					tt_buffer_empty(&arg_buf);
					term->spacetail = !(*end == '\0');
					if (term->command_args == NULL) {
						term->command_args = p_new;
					} else {
						p_tail->next = p_new; /* it's impossible that p_tail is NULL because term->command_args must be NULL at first loop */
					}
					p_tail = p_new;
					p_new = NULL;
				}
				if (*end == '\0') {
					eof = 1;
					break;
				}
				start = end;
			} else {
				if (*end == '\0') {
					eof = 1;
					break;
				}
				/* found arg content */
				tt_buffer_write(&arg_buf, end, 1);
			}
		}  /* end of parse one argumeng */
		if (eof) { /* end of line */
			break;
		}
	}
	if (term->event != E_EVENT_COMPLETE) {
		if (in_quot || backslash_tail) {
			/* save current line content to prefix and return 1 to continue read */
			if (backslash_tail) { /* skip '\\' */
				tt_buffer_write(&(term->prefix), term->line_command.content, term->line_command.used - 1);
			} else {
				tt_buffer_write(&(term->prefix), term->line_command.content, term->line_command.used);
			}
			if (in_quot) {
				tt_buffer_write(&(term->prefix), "\n", 1);
			}
			ret = -1; /* return -1 means continue */
		}
		term->multiline = ret;
	}
func_end:
	tt_buffer_free(&arg_buf);
	tt_buffer_free(&command_buf);
	if (p_new != NULL) {
		if (p_new->content != NULL) {
			MY_FREE(p_new->content);
		}
		MY_FREE(p_new);
	}
	return ret;
}

static int term_getch(Terminal *term) {
	int ret = 0;
	char key = 0;
	while (1) {
		ret = term->read(term, &key, 1);
		if (ret < 0) {
			perror("term->read()");
			break;
		}
		if (ret == 0) {
			continue;
		}
		break;
	}
	// printf("%3d 0x%02x (%c)\n", key, key, isprint(key) ? key : ' ');
	return key;
}

static void term_screen_get(Terminal *term, int *cols, int *rows) {
#if defined(_WIN32)
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo (GetStdHandle(STD_OUTPUT_HANDLE), &inf);
	*cols = inf.srWindow.Right - inf.srWindow.Left + 1;
	*rows = inf.srWindow.Bottom - inf.srWindow.Top + 1;
#else
	struct winsize ws = {0};

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	*cols = ws.ws_col;
	*rows = ws.ws_row;
#endif
	*cols = *cols > 1 ? *cols : 80;
	*rows = *rows > 1 ? *rows : 24;
}

static void term_cursor_move(Terminal *term, int col_off, int row_off) {
#if defined(_WIN32)
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo (GetStdHandle(STD_OUTPUT_HANDLE), &inf);
	inf.dwCursorPosition.Y += (SHORT)row_off;
	inf.dwCursorPosition.X += (SHORT)col_off;
	SetConsoleCursorPosition (GetStdHandle(STD_OUTPUT_HANDLE), inf.dwCursorPosition);
#else
	if (row_off > 0) {
		term_printf_inner(term, "\e[%dB", row_off);
	} else if (row_off < 0) {
		term_printf_inner(term, "\e[%dA", -row_off);
	}
	if (col_off > 0) {
		term_printf_inner(term, "\e[%dC", col_off);
	} else if (col_off < 0) {
		term_printf_inner(term, "\e[%dD", -col_off);
	}
#endif
}

void term_color_set(Terminal *term, unsigned int color) {
	term_printf_inner(term, "\033[0m");
	if (color & 0xff) {
		term_printf_inner(term, "\033[%dm", color & 0xff);
	}
	if (color & 0xff00) {
		term_printf_inner(term, "\033[%dm", (color & 0xff00) >> 8);
	}
	if (color & TERM_STYLE_BOLD) {
		term_printf_inner(term, "\033[1m");
	}
	if (color & TERM_STYLE_UNDERSCORE) {
		term_printf_inner(term, "\033[4m");
	}
	if (color & TERM_STYLE_BLINKING) {
		term_printf_inner(term, "\033[5m");
	}
	if (color & TERM_STYLE_INVERSE) {
		term_printf_inner(term, "\033[7m");
	}
}

int term_prompt_set(Terminal *term, const char *prompt) {
	int ret = -1;
	if (prompt == NULL) {
		prompt = term->default_prompt;
	}
	if (term->prompt != NULL) {
		MY_FREE(term->prompt);
		term->prompt = NULL;
	}
	if (prompt == NULL) {
		term->prompt = MY_STRDUP("");
	} else {
		term->prompt = MY_STRDUP(prompt);
	}
	ret = 0;
	return ret;
}

void term_prompt_color_set(Terminal *term, unsigned int color) {
	term->prompt_color = color;
}

void term_userdata_set(Terminal *term, void *userdata) {
	term->userdata = userdata;
}
void *term_userdata_get(Terminal *term) {
	return term->userdata;
}

static void term_print_prompt(Terminal *term) {
	if (!term->multiline) {
		term_color_set(term, term->prompt_color);
		term_printf_inner(term, term->prompt);
		term_printf_inner(term, " ");
		term_color_set(term, TERM_COLOR_DEFAULT);
	} else {
		term_printf_inner(term, "> ");
	}
	term->pos = 0;
}

static ssize_t read_std(Terminal *term, void *buf, size_t count) {
	ssize_t ret = 0;

#if defined(_WIN32)
	fflush(stdout);
	*(char *)buf = _getch();
	ret = 1;
#else
	struct termios old_term, cur_term;
	if (tcgetattr(STDIN_FILENO, &old_term) < 0) {
		perror("tcsetattr");
	}
	cur_term = old_term;
	cur_term.c_lflag &= ~(ICANON | ECHO | ISIG); // echoing off, canonical off, no signal chars
	cur_term.c_cc[VMIN] = 1;
	cur_term.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &cur_term) < 0) {
		perror("tcsetattr");
	}
	ret = read(STDIN_FILENO, buf, count);
	if (tcsetattr(STDIN_FILENO, TCSADRAIN, &old_term) < 0) {
		perror("tcsetattr");
	}
#endif
	return ret;
}

static ssize_t read_init_content(Terminal *term, void *buf, size_t count) {
	ssize_t ret = 0;
	if (term->init_content == NULL) {
		term->read = read_std;
		ret = 0;
	} else {
		*(char *)buf = *(term->init_content + term->init_content_offset);
		if (*(char *)buf == '\0') {
			MY_FREE(term->init_content);
			term->init_content = NULL;
			term->init_content_offset = 0;
			term->read = read_std;
			ret = 0;
		} else {
			term->init_content_offset++;
			ret = 1;
		}
	}
	return ret;
}

static ssize_t write_std(Terminal *term, const void *buf, size_t count) {
	return write(STDOUT_FILENO, buf, count);
}

static void wordhelp_free(struct TermWordHelp **p_head) {
	TermWordHelp *p_cur = NULL, *p_next = NULL;

	for (p_cur = (*p_head); p_cur != NULL; p_cur = p_next) {
		p_next = p_cur->next;
		if (p_cur->word != NULL) {
			MY_FREE(p_cur->word);
		}
		if (p_cur->help != NULL) {
			MY_FREE(p_cur->help);
		}
		MY_FREE(p_cur);
	}
}


void term_destroy(Terminal *term) {
	int i = 0;
	if (term->prompt != NULL) {
		MY_FREE(term->prompt);
	}
	if (term->default_prompt != NULL) {
		MY_FREE(term->default_prompt);
	}
	tt_buffer_free(&(term->prefix));
	tt_buffer_free(&(term->line_command));
	tt_buffer_free(&(term->tempbuf));
	for (i = 0; i < term->history_cnt; i++) {
		MY_FREE(term->history[i]);
	}
	MY_FREE(term->history);
	if (term->line != NULL) {
		MY_FREE(term->line);
	}
	term->history_cnt = 0;
	term->history = NULL;
	term_free_args(term);
	wordhelp_free(&(term->complete));
	wordhelp_free(&(term->hints));
	memset(term, 0x00, sizeof(Terminal));
	free(term);
}

int term_create(Terminal **_term, const char *prompt, TermNode *root, const char *init_content) {
	int ret = -1, not_support = 0;
	Terminal *term = NULL;

#if defined(_WIN32)
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
		return GetLastError();
	}

	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode)) {
		return GetLastError();
	}

	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode)){
		return GetLastError();
	}
#endif
	if (!isatty(STDIN_FILENO)) {  // input is not from a terminal
		not_support = 1;
	}
	if (not_support) {
		printf("not support\n");
		goto func_end;
	}

	term = (Terminal *)malloc(sizeof(Terminal));
	if (term == NULL) {
		goto func_end;
	}
	memset(term, 0x00, sizeof(Terminal));
	tt_buffer_init(&(term->line_command));
	tt_buffer_swapto_malloced(&(term->line_command), 0); /* avoid term->line_command->content is null */
	tt_buffer_init(&(term->tempbuf));
	tt_buffer_init(&(term->prefix));
	tt_buffer_swapto_malloced(&(term->prefix), 0); /* avoid term->frefix->content is null */
	term->default_prompt = MY_STRDUP(prompt);
	if (0 != term_prompt_set(term, prompt)) {
		goto func_end;
	}
	if (init_content != NULL) {
		term->init_content = MY_STRDUP(init_content);
		term->init_content_offset = 0;
		term->read = read_init_content;
	} else {
		term->read = read_std;
	}
	term->write = write_std;
	term->root = root;
	term_prompt_color_set(term, TERM_FGCOLOR_BRIGHT_GREEN | TERM_STYLE_BOLD);
	*_term = term;
	ret = 0;
func_end:
	if (ret != 0) {
		if (term != NULL) {
			term_destroy(term);
		}
	}
	return ret;
}

int term_root_set(Terminal *term, TermNode *root) {
	term->root = root;
	return 0;
}

static int term_getkey(Terminal *term) {
	int num1 = 0, num2 = 0;
	int key = 0;

#if defined(_WIN32)
	key = term_getch(term);
	if ((unsigned char)key == 0xe0) { /* extern */
		key = term_getch(term);
		switch (key) {
			case 0x47: return KEY_HOME;
			case 0x48: return KEY_UP;
			case 0x4b: return KEY_LEFT;
			case 0x4d: return KEY_RIGHT;
			case 0x4f: return KEY_END;
			case 0x50: return KEY_DOWN;
			case 0x73: return KEY_CTRL(KEY_LEFT);
			case 0x74: return KEY_CTRL(KEY_RIGHT);
		}
	} else if (key == 0x7f) {
		return KEY_BACKSPACE;
	}
#else
	key = term_getch(term);
	if (KEY_ESC == key) { /* need escape */
		key = term_getch(term);
		switch (key) {
			case '[':
				key = term_getch(term);
				if (key >= '0' && key <= '9') {
					do {
						num1 *= 10;
						num1 += key - '0';
						key = term_getch(term);
					} while (key >= '0' && key <= '9');
					switch (key) {
						case '~':
							switch (num1) { /* <ESC>1~	<ESC>[15~ */
								case 1: return KEY_HOME;
								case 2: return KEY_INSERT;
								case 3: return KEY_DELETE;
								case 4: return KEY_END;
								case 5: return KEY_PGUP;
								case 6: return KEY_PGDN;
								case 15:
								case 17:
								case 18:
								case 19:
								case 20:
								case 21:
								case 23:
								case 24: return KEY_FUN(num1 - 10);
								default: return -1;
							}
						case ';':
							key = term_getch(term);
							do {
								num2 *= 10;
								num2 += key - '0';
								key = term_getch(term);
							} while (key >= '0' && key <= '9');
							switch (num1) { /* <ESC>[1;3A <ESC>5;3~ */
								case 1:
									switch (num2) {
										case 5:
											switch (key) {
												case 'A': return KEY_CTRL(KEY_UP);
												case 'B': return KEY_CTRL(KEY_DOWN);
												case 'C': return KEY_CTRL(KEY_RIGHT);
												case 'D': return KEY_CTRL(KEY_LEFT);
												case 'F': return KEY_CTRL(KEY_END);
												case 'H': return KEY_CTRL(KEY_HOME);
												case 'P': return KEY_CTRL(KEY_FUN(1));
												case 'Q': return KEY_CTRL(KEY_FUN(2));
												case 'R': return KEY_CTRL(KEY_FUN(3));
												case 'S': return KEY_CTRL(KEY_FUN(4));
												default: return -1;
											}
									}
								case 5:
									if (num2 == 3 && key == '~') {
										return KEY_ALT(KEY_PGUP);
									}
									return -1;
								case 6:
									if (num2 == 3 && key == '~') {
										return KEY_ALT(KEY_PGDN);
									}
									return -1;
								default: return -1;
							}
						default: return -1;
					}
				} else {
					switch (key) { /* <ESC>[A */
						case 'A': return KEY_UP;
						case 'B': return KEY_DOWN;
						case 'C': return KEY_RIGHT;
						case 'D': return KEY_LEFT;
						case 'F': return KEY_END;
						case 'H': return KEY_HOME;
						default: return -1;
					}
				}
			case 'O':
				key = term_getch(term);
				switch (key) { /* <ESC>OP */
					case 'P': return KEY_FUN(1);
					case 'Q': return KEY_FUN(2);
					case 'R': return KEY_FUN(3);
					case 'S': return KEY_FUN(4);
					default: return -1;
				}
			default:
				return KEY_ALT(key);
		}
	} else if (key == 0x7f) {
		return KEY_BACKSPACE;
	}
#endif
	return key;
}

static void term_refresh(Terminal *term, int pos, int num, int refresh_pos) {
	int i = 0, prompt_len = 0, pos_row = 0, pos_col = 0;;
	int rows = 0, cols = 0;

	term_screen_get(term, &cols, &rows);
	prompt_len = strlen(term->prompt) + 1; /* 1: cursor and space after prompt */

	if (refresh_pos >= 0) { /* update changes */
		term->line_command.content[num] = '\0';
		pos_row = (refresh_pos + prompt_len) / cols - (term->pos + prompt_len) / cols;
		pos_col = (refresh_pos + prompt_len) % cols - (term->pos + prompt_len) % cols;
		term_cursor_move(term, pos_col, pos_row);
		for (i = refresh_pos; i < num; i++) {
			if (term->mask) {
				term_printf_inner(term, "*");
			} else {
				term_printf_inner(term, "%c", *((char *)(term->line_command.content) + i));
			}
			refresh_pos += 1;
			if ((refresh_pos + prompt_len) % cols == 0) { /* reach right border, new line */
				term_printf_inner(term, "\r\n");
			}
		}
		for (i = 0; i < term->num - num; i++) {
			refresh_pos += 1;
			term_printf_inner(term, " ");
			if ((refresh_pos + prompt_len) % cols == 0) { /* reach right border, new line */
				term_printf_inner(term, "\r\n");
			}
		}
		pos_row = (pos + prompt_len) / cols - (refresh_pos + prompt_len) / cols;
		pos_col = (pos + prompt_len) % cols - (refresh_pos + prompt_len) % cols;
		term_cursor_move(term, pos_col, pos_row);
	} else { /* move cursor only */
		pos_row = (pos + prompt_len) / cols - (term->pos + prompt_len) / cols;
		pos_col = (pos + prompt_len) % cols - (term->pos + prompt_len) % cols;
		term_cursor_move(term, pos_col, pos_row);
	}
	term->pos = pos;
	term->num = num;
	term->line_command.used = num;
}

static void term_wordhelp_free(TermWordHelp **wordhelp) {
	TermWordHelp *p_next = NULL, *p_cur = NULL;

	for (p_cur = *wordhelp; p_cur != NULL; p_cur = p_next) {
		p_next = p_cur->next;
		MY_FREE(p_cur->word);  /* word must not be NULL */
		if (p_cur->help != NULL) {
			MY_FREE(p_cur->help);
		}
		MY_FREE(p_cur);
	}
	*wordhelp = NULL;
}

static void term_wordhelp_add(TermWordHelp **wordhelp, const char *word, const char *help) {
	int ret = -1;
	TermWordHelp *p_new = NULL, *p_cur = NULL;

	p_new = (TermWordHelp *)MY_MALLOC(sizeof(TermWordHelp));
	if (p_new == NULL) {
		goto func_end;
	}
	memset(p_new, 0x00, sizeof(TermWordHelp));
	if (word == NULL) { /* word must not be NULL */
		goto func_end;
	}
	p_new->word = MY_STRDUP(word);
	if (p_new->word == NULL) {
		goto func_end;
	}
	if (help != NULL) {
		p_new->help = (char *)MY_STRDUP(help);
		if (p_new->help == NULL) {
			goto func_end;
		}
	}
	if (*wordhelp == NULL) {
		*wordhelp = p_new;
	} else {
		for (p_cur = *wordhelp; p_cur->next != NULL; p_cur = p_cur->next);
		p_cur->next = p_new;
	}
	ret = 0;
func_end:
	if (ret != 0) {
		if (p_new != NULL) {
			if (p_new->word != NULL) {
				MY_FREE(p_new->word);
			}
			if (p_new->help != NULL) {
				MY_FREE(p_new->help);
			}
			MY_FREE(p_new);
		}
	}
	return;
}

static void term_complete_free(Terminal *term) {
	term_wordhelp_free(&(term->complete));
}

static void term_complete_add(Terminal *term, const char *word, const char *help) {
	term_wordhelp_add(&(term->complete), word, help);
}

static void term_hints_free(Terminal *term) {
	term_wordhelp_free(&(term->hints));
}

static void term_hints_add(Terminal *term, const char *word, const char *help) {
	term_wordhelp_add(&(term->hints), word, help);
}

static int strlenwithesc(const char *s) {
	int ret = 0;
	const char *cur = NULL;
	for (cur = s; *cur != '\0'; cur++) {
		if (*cur == '\n' || *cur == ' ' || *cur == '\\' || *cur == '\'' || *cur == '\"') {
			ret += 2;
		} else {
			ret += 1;
		}
	}
	return ret;
}

static char *strcatwithesc(char *dest, const char *src) {
	char *dst_cur = NULL;
	const char *src_cur = NULL;
	for (dst_cur = dest; *dst_cur != '\0'; dst_cur++);
	for (src_cur = src; *src_cur != '\0'; src_cur++) {
		if (*src_cur == '\n') {
			*dst_cur = '\\';
			dst_cur++;
			*dst_cur = 'n';
			dst_cur++;
		} else if(*src_cur == ' ' || *src_cur == '\\' || *src_cur == '\'' || *src_cur == '\"') {
			*dst_cur = '\\';
			dst_cur++;
			*dst_cur = *src_cur;
			dst_cur++;
		} else {
			*dst_cur = *src_cur;
			dst_cur++;
		}
	}
	*dst_cur = '\0';
	return dest;
}

static void term_history_add(Terminal *term) {
	size_t len = 0;
	TermArg *p_arg = NULL;

	if (term->history == NULL) {
		term->history = (char **)MY_MALLOC(sizeof(char *) * HISTORY_LENGTH);
		if (term->history == NULL) {
			goto func_end;
		}
		memset(term->history, 0x00, sizeof(char *) * HISTORY_LENGTH);
	}
	if (term->command_args == NULL) {
		goto func_end;
	}
	len = 0;
	for (p_arg = term->command_args; p_arg != NULL; p_arg = p_arg->next) {
		len += strlenwithesc(p_arg->content) + 1;
	}
	if (term->history_cnt < HISTORY_LENGTH) {
		term->history_cnt += 1;
	} else {
		MY_FREE(term->history[0]);
		memmove(&term->history[0], &term->history[1], HISTORY_LENGTH - 1);
	}
	term->history[term->history_cnt - 1] = (char *)MY_MALLOC(len);
	if (term->history[term->history_cnt - 1] == NULL) {
		goto func_end;
	}
	term->history[term->history_cnt - 1][0] = '\0';
	for (p_arg = term->command_args; p_arg != NULL; p_arg = p_arg->next) {
		if (term->history[term->history_cnt - 1][0] != '\0') {
			strcat(term->history[term->history_cnt - 1], " ");
		}
		strcatwithesc(term->history[term->history_cnt - 1], p_arg->content);
	}
func_end:
	return;
}


static int compare_keyword(const char *target, const char *content) {
	if (target == NULL || content == NULL) {
		return MATCH_ALL;
	}
	if (0 == strcasecmp(content, target)) {
		return MATCH_ALL;
	}
	if (0 == strncasecmp(content, target, strlen(content))) {
		return MATCH_PART;
	}
	return MATCH_NONE;
}

static void term_output_complete_or_help(Terminal *term) {
	int i = 0, common_len = 0, tail_arglen = 0, start_pos = 0, end_pos = 0, completed = 0;
	int word_width = 0, with_help = 0, rows = 0, cols = 0, words_len = 0, word_num = 0;
	TermComplete *p_com = NULL;
	TermArg *p_lastarg = NULL;
	int executable = (term->exec_num == 1);

	if (term->complete != NULL) {
		common_len = (int)strlen(term->complete->word);
		// find common string for auto complete
		for (p_com = term->complete->next; (p_com != NULL) && (common_len > 0); p_com = p_com->next) {
			while ((common_len > 0) && strncasecmp(term->complete->word, p_com->word, common_len)) {
				common_len--;
			}
		}
		if (common_len > 0) {
			tail_arglen = 0;
			p_lastarg = NULL;
			if (term->command_args != NULL) {
				for (p_lastarg = term->command_args; p_lastarg->next != NULL; p_lastarg = p_lastarg->next);
			}
			if (p_lastarg && !term->spacetail && term->command_args != NULL) {
				tail_arglen = strlen(p_lastarg->content);
			}
			if (tail_arglen > 0) { /* null arg for help */
				start_pos = term->num - tail_arglen;
				end_pos = start_pos + common_len;
				if (common_len > tail_arglen) {
					/* malloc for complete, command_len - tail_arglen is the size that need expand */
					tt_buffer_swapto_malloced(&(term->line_command), common_len - tail_arglen);
				}
				if (memcmp(term->line_command.content + start_pos, term->complete->word, common_len)) {
					memcpy(term->line_command.content + start_pos, term->complete->word, common_len);
					completed = 1;
				}
				term->line_command.used += common_len - tail_arglen;
				*(term->line_command.content + end_pos) = '\0';
				if (term->complete->next == NULL) { /* only one match, add SPACE at the end of word */
					tt_buffer_swapto_malloced(&(term->line_command), 1); /* malloc for complete SPACE */
					strcat((char *)(term->line_command.content), " ");
					end_pos += 1;
					term->line_command.used += 1;
					completed = 1;
				}
				// printf("term->pos %d, start_pos %d, end_pos %d\n", term->pos, start_pos, end_pos);
				term_refresh(term, end_pos, end_pos, start_pos);
			}
		}
	}
	if (completed == 0) { /* print help informations */
		with_help = 0;
		word_width = 0;
		for (p_com = term->complete; p_com != NULL; p_com = p_com->next) {
			if (p_com->help != NULL && p_com->help[0] != '\0') {
				with_help = 1;
			}
			if ((int)strlen(p_com->word) > word_width) {
				word_width = strlen(p_com->word);
			}
		}
		for (p_com = term->hints; p_com != NULL; p_com = p_com->next) {
			if (p_com->help != NULL && p_com->help[0] != '\0') {
				with_help = 1;
			}
			if ((int)strlen(p_com->word) > word_width) {
				word_width = strlen(p_com->word);
			}
		}
		term_printf_inner(term, "\n");
		if (with_help) { /* print word and help line by line */
			for (p_com = term->complete; p_com != NULL; p_com = p_com->next) {
				term_color_set(term, TERM_FGCOLOR_BRIGHT_BLUE);
				term_printf_inner(term, "%s", p_com->word);
				term_color_set(term, TERM_COLOR_DEFAULT);
				if (p_com->help != NULL) {
					term_printf_inner(term, "%*s	 %s\n", word_width - strlen(p_com->word), "", p_com->help);
				} else {
					term_printf_inner(term, "\n"); /* show word only if help is NULL */
				}
			}
			for (p_com = term->hints; p_com != NULL; p_com = p_com->next) {
				term_color_set(term, TERM_FGCOLOR_BRIGHT_CYAN);
				term_printf_inner(term, "%s", p_com->word);
				term_color_set(term, TERM_COLOR_DEFAULT);
				if (p_com->help != NULL) {
					term_printf_inner(term, "%*s	 %s\n", word_width - strlen(p_com->word), "", p_com->help);
				} else {
					term_printf_inner(term, "\n");
				}
			}
		} else { /* only show words */
			term_screen_get(term, &cols, &rows);
			words_len = 0;
			i = 0;
			for (p_com = term->complete; p_com != NULL; p_com = p_com->next, i++) {
				if (i != 0) {
					words_len += 2;
				}
				words_len += strlen(p_com->word);
			}
			for (p_com = term->hints; p_com != NULL; p_com = p_com->next, i++) {
				if (i != 0) {
					words_len += 2;
				}
				words_len += strlen(p_com->word);
			}
			if (words_len <= cols) {
				i = 0;
				for (p_com = term->complete; p_com != NULL; p_com = p_com->next, i++) {
					if (i != 0) {
						term_printf_inner(term, "  ");
					}
					term_color_set(term, TERM_FGCOLOR_BRIGHT_BLUE);
					term_printf_inner(term, "%s", p_com->word);
					term_color_set(term, TERM_COLOR_DEFAULT);
				}
				for (p_com = term->hints; p_com != NULL; p_com = p_com->next, i++) {
					if (i != 0) {
						term_printf_inner(term, "  ");
					}
					term_color_set(term, TERM_FGCOLOR_BRIGHT_CYAN);
					term_printf_inner(term, "%s", p_com->word);
					term_color_set(term, TERM_COLOR_DEFAULT);
				}
				if (i != 0) {
					term_printf_inner(term, "\n");
				}
			} else { /* print word as a table */
				word_num = ((cols - word_width) / (word_width + 2)) + 1;
				i = 0;
				for (p_com = term->complete; p_com != NULL; p_com = p_com->next, i++) {
					if (i % word_num == 0) {
						term_printf_inner(term, "%s", i ? "\n" : "");
					} else {
						term_printf_inner(term, "  ");
					}
					term_color_set(term, TERM_FGCOLOR_BRIGHT_BLUE);
					term_printf_inner(term, "%s", p_com->word);
					term_color_set(term, TERM_COLOR_DEFAULT);
					term_printf_inner(term, "%*s", word_width - strlen(p_com->word), "");
				}
				for (p_com = term->hints; p_com != NULL; p_com = p_com->next, i++) {
					if (i % word_num == 0) {
						term_printf_inner(term, "%s", i ? "\n" : "");
					} else {
						term_printf_inner(term, "  ");
					}
					term_color_set(term, TERM_FGCOLOR_BRIGHT_CYAN);
					term_printf_inner(term, "%s", p_com->word);
					term_color_set(term, TERM_COLOR_DEFAULT);
					term_printf_inner(term, "%*s", word_width - strlen(p_com->word), "");
				}
				if (i != 0) {
					term_printf_inner(term, "\n");
				}
			}
		} /* end only show words */
		if (executable) {
			term_printf_inner(term, "<CR>\n");
		}
		term_print_prompt(term);
		// printf("term->pos %d, line_command %d\n", term->pos, strlen((char *)(term->line_command.content)));
		term_refresh(term, strlen((char *)(term->line_command.content)), strlen((char *)(term->line_command.content)), 0);
	}
}

static TermNode *node_get_option(TermNode *node) {
	if (node->type != TYPE_SELECT && node->type != TYPE_MULSEL) {
		return NULL;
	}
	return node->option;
}
static TermNode *node_get_unmasked_option(TermNode *node, uint64_t mask) {
	TermNode *cur = NULL;
	if (node->selector == NULL || node->selector->type != TYPE_MULSEL) {
		goto func_end;
	}
	for (cur = node->selector->option; cur != NULL; cur = cur->next) {
		if ((mask & (1 << cur->option_index)) == 0) {
			break;
		}
	}
func_end:
	return cur;
}
static TermExec node_executable(TermNode *node) {
	if (node->selector != NULL && (node->selector->type == TYPE_SELECT || node->selector->type == TYPE_MULSEL)) {
		return node->selector->exec;
	}
	return node->exec;
}

static void node_free(TermNode *node) {
	TermNode *p_node = NULL, *p_next = NULL;
	for (p_node = node->children; p_node != NULL; p_node = p_next) {
		p_next = p_node->next;
		node_free(p_node);
	}
	for (p_node = node->option; p_node != NULL; p_node = p_next) {
		p_next = p_node->next;
		node_free(p_node);
	}
	MY_FREE(node->word);
	if (node->help != NULL) {
		MY_FREE(node->help);
	}
	MY_FREE(node);
}

static void update_node_options(TermNode *selector, TermDynOptionCb dyncb, void *userdata) {
	TermNode *p_node = NULL, *p_next = NULL;
	char **word = NULL, **help = NULL;
	int i = 0, num = 0;
	for (p_node = selector->option; p_node != NULL; p_node = p_next) {
		p_next = p_node->next;
		node_free(p_node);
	}
	selector->option= NULL;
	dyncb(userdata, &word, &help, &num);
	for (i = 0; i < num; i++) {
		if (term_node_option_add(selector, word[i], help != NULL ? help[i] : NULL) == NULL) {
			goto func_end;
		}
	}
func_end:
	if (word != NULL) {
		for (i = 0; i < num; i++) {
			free(word[i]);
		}
		free(word);
	}
	if (help != NULL) {
		for (i = 0; i < num; i++) {
			free(help[i]);
		}
		free(help);
	}
	return;
}
static void term_exec_run(Terminal *term, WalkStacked *stacked, int deep) {
	int i = 0, j = 0, argc = 0, mulsel_len = 0;
	char **argv = NULL;
	int *argv_alloced = NULL; /* argv is alloced or not */
	uint64_t checked = 0;
	TermNode *cur = NULL;
	
	term->exec_num++;

	if (term->event != E_EVENT_EXEC) {
		goto func_end;
	}
	/* generate argv */
	argc = 0;
	for (i = 0; i < deep + 1; i++) {
		if (stacked[i].node->type == TYPE_KEY || stacked[i].node->type == TYPE_TEXT) {
			argc++;
		}
	}
	argv = (char **)malloc(sizeof(char *) * argc);
	argv_alloced = (int *)malloc(sizeof(int) * argc);
	memset(argv_alloced, 0x00, sizeof(int) * argc);
	for (i = 0, j = 0; i < deep + 1; ) {
		switch (stacked[i].node->type) {
			case TYPE_TEXT: argv[j] = stacked[i].exec_argv; i++; j++; break;
			case TYPE_KEY: argv[j] = stacked[i].node->word; i++; j++; break;
			case TYPE_SELECT: argv[j] = stacked[i + 1].node->word; i += 2; j++; break; /* += 2 to skip options */
			case TYPE_MULSEL:
				/* calc mulsel_len */
				checked = stacked[i].checked;
				for (mulsel_len = 0, cur = stacked[i].node->option; checked != 0; cur = cur->next) {
					if (checked & 1) {
						if (mulsel_len > 0) {
							mulsel_len += 1; /* join with '+' */
						}
						mulsel_len += strlen(cur->word);
					}
					checked >>= 1;
				}

				/* join */
				argv[j] = malloc(mulsel_len + 1); /* len + 1 for '\0' */
				argv_alloced[j] = 1;
				argv[j][0] = '\0';
				checked = stacked[i].checked;
				for (cur = stacked[i].node->option; checked != 0; cur = cur->next) {
					if (checked & 1) {
						if (argv[j][0] != '\0') {
							strcat(argv[j], "+");
						}
						strcat(argv[j], cur->word);
					}
					checked >>= 1;
				}
				i += 2; /* skip options */
				j++;
				break;
			default: ;
		}
	}

	/* run exec func */
	node_executable(stacked[deep].node)(term, argc, (const char **)argv);

	for (i = 0; i < argc; i++) {
		if (argv_alloced[i]) {
			free(argv[i]);
		}
	}
	free(argv_alloced);
	free(argv);
func_end:
	return;
}
#define WALK_DEBUG 0
static void term_walk(Terminal *term) {
	int match = 0, deep = 0;
	WalkStacked stacked[WALK_MAX_DEEP];
	TermNode *node = NULL, *next = NULL;
	TermArg *arg = NULL;

	term->exec_num = 0;

	memset(&stacked, 0x00, sizeof(stacked));
	stacked[0].node = term->root->children;
	stacked[0].arg = term->command_args;

	/* walk all nodes */
	while (1) {
		node = stacked[deep].node;
		arg = stacked[deep].arg;
		if (node->dyn_option != NULL) {
			update_node_options(node, node->dyn_option, node->dyn_option);
		}
#if WALK_DEBUG
		printf("deep:%d, arg:%s =? %s\n", deep, arg ? arg->content : "null", node ? node->word : "null");
#endif
		match = MATCH_NONE;

		if ((node->type == TYPE_SELECT || node->type == TYPE_MULSEL) && node_get_option(node) != NULL) { /* process children in selector */
			if (stacked[deep].optional == 0) {
				stacked[deep].walked = 0;
				stacked[deep].checked = 0;
				stacked[deep + 1].node = node_get_option(node);
				stacked[deep + 1].arg = arg;
				deep++;
				continue;
			} else {
				if (arg == NULL) {
					stacked[deep + 1].node = node_get_option(node);
					stacked[deep + 1].arg = arg;
					deep++;
					term_exec_run(term, stacked, deep);
					memset(&stacked[deep], 0x00, sizeof(WalkStacked));
					deep--;
				}
				if (node->children != NULL) {
					stacked[deep + 1].node = node_get_option(node);
					stacked[deep + 1].arg = arg;
					deep++;
					stacked[deep + 1].node = node->children;
					stacked[deep + 1].arg = arg;
					deep++;
					continue;
				}
			}
		}
		if (node->selector != NULL && node->selector->type == TYPE_MULSEL) {
			stacked[deep - 1].walked |= (1 << node->option_index);
		}

		switch (node->type) {
			case TYPE_KEY:
				if (arg == NULL) {
					 /* is last arg of input */
					match = MATCH_ALL;
					term_complete_add(term, node->word, node->help);
					break; /* break switch */
				}
				match = compare_keyword(node->word, arg->content);
				if (match != MATCH_NONE) {
					if (arg->next == NULL) {
						if (!term->spacetail) { /* need complete or print hellp */
							term_complete_add(term, node->word, node->help);
						}
					}
				}
				break;
			case TYPE_TEXT:
				match = MATCH_ALL;
				if (arg == NULL) {
					 /* is last arg of input */
					term_hints_add(term, node->word, node->help);
					break; /* break switch */
				}
				stacked[deep].exec_argv = arg->content;
				if (arg->next == NULL) {
					if (!term->spacetail) { /* need complete or print help */
						term_hints_add(term, node->word, node->help);
					}
				}
				break;
			default: ;
		}
#if WALK_DEBUG
		printf("match %d %d\n", match, arg != NULL && (arg->next || term->spacetail));
#endif
		if (match == MATCH_ALL && arg != NULL) {
			if (node->selector != NULL && node->selector->type == TYPE_MULSEL) {
				stacked[deep - 1].checked |= (1 << node->option_index);
			}
			if (node_executable(node) != NULL && arg->next == NULL) {
				term_exec_run(term, stacked, deep);
			}
			if (arg->next != NULL || term->spacetail) {
				/* current match, and need check children */
				if (node->selector != NULL) { /* is option in TYPE_SELECT or TYPE_MULSEL */
					if (node->selector->type == TYPE_SELECT) {
						if (node->selector->children != NULL) {
							stacked[deep + 1].node = node->selector->children;
							stacked[deep + 1].arg = arg->next;
							deep++;
							continue;
						}
					} else { /* node->selector->type == TYPE_MULSEL */
						stacked[deep].arg = arg->next; /* match arg->next for another option */
						stacked[deep - 1].walked = stacked[deep - 1].checked; /* skip checked option while next walk */
						if (node->selector->children != NULL) {
							stacked[deep + 1].node = node->selector->children;
							stacked[deep + 1].arg = arg->next;
							deep++;
							continue;
						}
					}
					break;
				} else {
					if (node->children != NULL) {
						stacked[deep].exec_argv = arg->content;
						stacked[deep + 1].node = node->children;
						stacked[deep + 1].arg = arg->next;
						deep++;
						continue;
					}
					break;
				}
			}
		}

		/* walk next node */
		if (node->type == TYPE_MULSEL && (node->flags & MULSEL_OPTIONAL) && stacked[deep].optional == 0) {
			stacked[deep].optional = 1;
			stacked[deep].checked = 0;
			stacked[deep].walked = ~0;
			next = node;
		} else if (node->selector != NULL && node->selector->type == TYPE_MULSEL) {
			next = node_get_unmasked_option(node, stacked[deep - 1].walked);
		} else {
			next = node->next;
		}
		while (next == NULL) {
			/* switch to uncle */
			memset(&stacked[deep], 0x00, sizeof(WalkStacked));
			deep--;
			if (deep < 0) {
				break;
			}
			node = stacked[deep].node;
			if (node->type == TYPE_MULSEL && (node->flags & MULSEL_OPTIONAL) && stacked[deep].optional == 0) {
				stacked[deep].optional = 1;
				stacked[deep].checked = 0;
				stacked[deep].walked = ~0;
				next = node;
			} else if (node->selector != NULL && node->selector->type == TYPE_MULSEL) { /* one option in mulsel walked */
				next = node_get_unmasked_option(node, stacked[deep - 1].walked);
			} else {
				next = node->next;
			}
		}
		if (deep < 0) {
			break;
		}
		stacked[deep].node = next;
	}
	/* all nodes walked */

	/* maybe found completion and help info */
	if (term->event == E_EVENT_COMPLETE) {
		term_output_complete_or_help(term);
	} else if (term->event == E_EVENT_EXEC) {
		term_history_add(term);
		if (term->exec_num == 0) {
			printf("command not found.\n");
		} else if (term->exec_num > 1) {
			printf("WARN: %d commands executed.\n", term->exec_num);
		}
		if (!term->exit_flag) {
			term_print_prompt(term);
			term_refresh(term, 0, 0, 0);
		}
	}
	term->event = E_EVENT_NONE;

	term_complete_free(term);
	term_hints_free(term);
}

void term_exit(Terminal *term) {
	term->exit_flag = 1;
}

static const char *term_getline_inner(Terminal *term, const char *prefix, int mask) {
	int key = 0;
	char *old_prompt = NULL;
	unsigned int old_color = 0;
	old_prompt = MY_STRDUP(term->prompt);
	old_color = term->prompt_color;
	term_prompt_set(term, prefix);
	term_prompt_color_set(term, TERM_COLOR_DEFAULT);
	term->mask = mask;
	term_print_prompt(term);
	term_refresh(term, 0, 0, 0);
	while (1) { /* loop once every key press */
		key = term_getkey(term);
		switch (key) {
			/* move */
			case KEY_LEFT:
			case KEY_CTRL('B'):
				if (term->pos > 0) {
					term_refresh(term, term->pos - 1, term->num, -1);
				}
				break;
			case KEY_RIGHT:
			case KEY_CTRL('F'):
				if (term->pos < (int)(term->line_command.used)) {
					term_refresh(term, term->pos + 1, term->num, -1);
				}
				break;
			/* edit */
			case KEY_BACKSPACE: // Delete char to left of cursor
				if (term->pos > 0) {
					memmove(term->line_command.content + term->pos - 1, term->line_command.content + term->pos, term->line_command.used - term->pos + 1);
					term_refresh(term, term->pos - 1, term->num - 1, term->pos - 1);
				}
				break;
			case KEY_DELETE: // Delete character under cursor
			case KEY_CTRL('D'):
				if (term->pos < (int)(term->line_command.used)) {
					memmove(term->line_command.content + term->pos, term->line_command.content + term->pos + 1, term->line_command.used - term->pos);
					term->line_command.used -= 1;
					term_refresh(term, term->pos, term->num - 1, term->pos);
				}
				break;
			case KEY_CR:
			case KEY_LF:
				term_printf_inner(term, "\n");
				if (term->line != NULL) {
					MY_FREE(term->line);
				}
				term->line = MY_STRDUP((char *)term->line_command.content);
				goto func_end;
			case KEY_CTRL('C'):
				if (term->num > 0) {
					term_printf_inner(term, "^C\n");
					tt_buffer_empty(&(term->prefix));
					term_print_prompt(term);
					term_refresh(term, 0, 0, 0);
				} else {
					term_printf_inner(term, "\n");
					if (term->line != NULL) {
						MY_FREE(term->line);
						term->line = NULL;
					}
					goto func_end;
				}
				break;
			default:
				if (key >= ' ' && key <= '~') { /* key value may be too large, must not use isprint(key) */
					tt_buffer_swapto_malloced(&(term->line_command), 1);  /* malloc for complete SPACE */
					memmove(term->line_command.content + term->pos + 1, term->line_command.content + term->pos, term->num - term->pos);
					*(term->line_command.content + term->pos) = (char)key;
					term_refresh(term, term->pos + 1, term->num + 1, term->pos);
				} else {
					// printf("unhandler key: %08x\n", key);
				}
				break;
		} /* end of switch(key) */
	}
func_end:
	term_prompt_set(term, old_prompt);
	MY_FREE(old_prompt);
	term_prompt_color_set(term, old_color);
	term->mask = 0;
	return term->line;
}

int term_loop(Terminal *term) {
	int key = 0;
	int length = 0, new_pos = 0;

	term_print_prompt(term);
	term_refresh(term, 0, 0, 0);
	while (1) { /* loop once every key press */
		key = term_getkey(term);
		switch (key) {
			/* move */
			case KEY_LEFT:
			case KEY_CTRL('B'):
				if (term->pos > 0) {
					term_refresh(term, term->pos - 1, term->num, -1);
				}
				break;
			case KEY_RIGHT:
			case KEY_CTRL('F'):
				if (term->pos < (int)(term->line_command.used)) {
					term_refresh(term, term->pos + 1, term->num, -1);
				}
				break;
			case KEY_CTRL('A'): // Move cursor to start of line.
			case KEY_HOME:
				term_refresh(term, 0, term->num, -1);
				break;
			case KEY_CTRL('E'): // Move cursor to end of line
			case KEY_END:
				term_refresh(term, term->num, term->num, -1);
				break;
			case KEY_ALT('b'):	// Move back a word.
			case KEY_ALT('B'):
			case KEY_ALT(KEY_LEFT):
			case KEY_CTRL(KEY_LEFT):
				if (term->pos > 0) {
					for (new_pos = term->pos; (new_pos > 0) && isdelimiter(*(term->line_command.content + new_pos - 1)); new_pos--);
					for (; (new_pos > 0) && !isdelimiter(*(term->line_command.content + new_pos - 1)); new_pos--);
					term_refresh(term, new_pos, term->num, -1);
				}
				break;
			case KEY_ALT('f'):	 // Move forward a word.
			case KEY_ALT('F'):
			case KEY_ALT(KEY_RIGHT):
			case KEY_CTRL(KEY_RIGHT):
				if (term->pos < term->num) {
					for (new_pos = term->pos; (new_pos < term->num) && isdelimiter(*(term->line_command.content + new_pos)); new_pos++);
					for (; (new_pos < term->num) && !isdelimiter(*(term->line_command.content + new_pos)); new_pos++);
					for (; (new_pos < term->num) && isdelimiter(*(term->line_command.content + new_pos)); new_pos++);
					term_refresh(term, new_pos, term->num, -1);
				}
				break;

			/* history */
			case KEY_UP:
			case KEY_DOWN:
				if (key == KEY_UP) {
					if (term->history_cur == -1) {
						term->history_cur = term->history_cnt - 1;
					} else {
						term->history_cur -= 1;
					}
				} else {
					if (term->history_cur + 1 >= term->history_cnt) {
						term->history_cur = -1;
					} else {
						term->history_cur += 1;
					}
				}
				if (term->history_cur != -1) {
					term->line_command.used = 0;
					term_command_write(term, term->history[term->history_cur], strlen(term->history[term->history_cur]));
					length = term->line_command.used;
					term_refresh(term, length, length, 0);
				} else {
					term_refresh(term, 0, 0, 0);
				}
				break;

			/* complete */
			case KEY_TAB:		// Autocomplete (same with KEY_CTRL('I'))
				term->event = E_EVENT_COMPLETE;
				term_split_args(term);
				term_walk(term);
				break;

			/* edit */
			case KEY_BACKSPACE: // Delete char to left of cursor
				if (term->pos > 0) {
					memmove(term->line_command.content + term->pos - 1, term->line_command.content + term->pos, term->line_command.used - term->pos + 1);
					term_refresh(term, term->pos - 1, term->num - 1, term->pos - 1);
				}
				break;
			case KEY_DELETE: // Delete character under cursor
			case KEY_CTRL('D'):
				if (term->pos < (int)(term->line_command.used)) {
					memmove(term->line_command.content + term->pos, term->line_command.content + term->pos + 1, term->line_command.used - term->pos);
					term->line_command.used -= 1;
					term_refresh(term, term->pos, term->num - 1, term->pos);
				} else if ((0 == term->line_command.used) && (key == KEY_CTRL('D'))) { // If an empty line, EOF
					term_printf_inner(term, "exit because Ctrl+D\n");
					goto func_end;
				}
				break;
			case KEY_CR:
			case KEY_LF:
				term_printf_inner(term, "\n");
				if (term->line_command.used > 0) {
					if (term_split_args(term) == 0) {
						term->event = E_EVENT_EXEC;
						term_walk(term);
					} else {
						term_print_prompt(term);
						term_refresh(term, 0, 0, 0);
					}
				} else {
					if (term->multiline && term_split_args(term) == 0) { /* function need return while in multiline mode */
						term->event = E_EVENT_EXEC;
						term_walk(term);
					} else {
						term_print_prompt(term);
						term_refresh(term, 0, 0, 0);
					}
				}
				term->history_cur = -1;
				break;
			case KEY_CTRL('C'):
			case KEY_CTRL('G'):
				if (term->multiline) {
					term->multiline = 0;
					term_printf_inner(term, "%s\n", (key == KEY_CTRL('C')) ? "^C" : "^G");
					tt_buffer_empty(&(term->prefix));
					term_print_prompt(term);
					term_refresh(term, 0, 0, 0);
				} else {
					if (term->num > 0) {
						term_printf_inner(term, "%s\n", (key == KEY_CTRL('C')) ? "^C" : "^G");
						term_print_prompt(term);
						term_refresh(term, 0, 0, 0);
					} else {
						term_printf_inner(term, "exit because Ctrl+%s\n", (key == KEY_CTRL('C')) ? "C" : "G");
						goto func_end;
					}
				}
				break;
			case KEY_CTRL('Z'):
#if defined(_WIN32)
				term_printf_inner(term, "exit because Ctrl+Z\n");
				goto func_end;
#else
				raise(SIGSTOP);
#endif
				break;
			default:
				if (key >= ' ' && key <= '~') { /* key value may be too large, must not use isprint(key) */
					tt_buffer_swapto_malloced(&(term->line_command), 1);  /* malloc for complete SPACE */
					memmove(term->line_command.content + term->pos + 1, term->line_command.content + term->pos, term->num - term->pos);
					*(term->line_command.content + term->pos) = (char)key;
					term_refresh(term, term->pos + 1, term->num + 1, term->pos);
				} else {
					// printf("unhandler key: %08x\n", key);
				}
				break;
		} /* end of switch(key) */
		if (term->exit_flag) {
			break;
		}
	}
func_end:
	term_free_args(term);
	return 0;
}
TermNode *term_root_create() {
	TermNode *root = NULL;
	root = MY_MALLOC(sizeof(TermNode));
	if (root == NULL) {
		goto func_end;
	}
	memset(root, 0x00, sizeof(TermNode));
func_end:
	return root;
}

void term_root_free(TermNode *root) {
	node_free(root);
}

TermNode *term_node_child_add(TermNode *parent, NodeType type, const char *word, const char *help, TermExec exec) {
	TermNode *new_node = NULL, *tail = NULL;

	new_node = MY_MALLOC(sizeof(TermNode));
	if (new_node == NULL || word == NULL) {
		goto func_end;
	}
	memset(new_node, 0x00, sizeof(TermNode));
	new_node->type = type;
	new_node->exec = exec;
	new_node->word = MY_STRDUP(word);
	if (help != NULL) {
		new_node->help = MY_STRDUP(help);
	}
	if (parent->children == NULL) {
		parent->children = new_node;
	} else {
		for (tail = parent->children; tail->next != NULL; tail = tail->next);
		tail->next = new_node;
	}
func_end:
	return new_node;
}
int term_node_child_del(TermNode *parent, const char *word) {
	TermNode *node = NULL, *pre = NULL;
	int found = 0;

	for (node = parent->children; node != NULL; node = node->next) {
		if (0 == strcmp(node->word, word)) {
			if (pre == NULL) {
				parent->children = node->next;
			} else {
				pre->next = node->next;
			}
			node_free(node);
			found = 1;
			break;
		}
		pre = node;
	}
	return !found;
}

TermNode *term_node_select_add(TermNode *parent, const char *word, TermExec exec) {
	return term_node_child_add(parent, TYPE_SELECT, word, NULL, exec);
}
TermNode *term_node_mulsel_add(TermNode *parent, const char *word, uint32_t flags, TermExec exec) {
	TermNode *new_node = NULL;
	new_node = term_node_child_add(parent, TYPE_MULSEL, word, NULL, exec);
	if (new_node == NULL) {
		goto func_end;
	}
	new_node->flags = flags;
func_end:
	return new_node;
}

TermNode *term_node_option_add(TermNode *selector, const char *word, const char *help) {
	TermNode *new_node = NULL, *tail = NULL;

	new_node = MY_MALLOC(sizeof(TermNode));
	if (new_node == NULL || word == NULL) {
		goto func_end;
	}
	memset(new_node, 0x00, sizeof(TermNode));
	new_node->type = TYPE_KEY;
	new_node->word = MY_STRDUP(word);
	if (help != NULL) {
		new_node->help = MY_STRDUP(help);
	}
	new_node->selector = selector;
	if (selector->option == NULL) {
		selector->option = new_node;
		new_node->option_index = 0;
	} else {
		for (tail = selector->option, new_node->option_index = 1; tail->next != NULL; tail = tail->next, new_node->option_index++);
		tail->next = new_node;
	}
func_end:
	return new_node;
}
int term_node_option_del(TermNode *selector, const char *word) {
	TermNode *node = NULL, *pre = NULL;
	int found = 0;

	for (node = selector->option; node != NULL; node = node->next) {
		if (0 == strcmp(node->word, word)) {
			if (pre == NULL) {
				selector->option = node->next;
			} else {
				pre->next = node->next;
			}
			node_free(node);
			found = 1;
			break;
		}
		pre = node;
	}
	return !found;
}

int term_node_dynamic_option(TermNode *selector, TermDynOptionCb cb_func, void *userdata) {
	selector->dyn_option = cb_func;
	selector->dyn_option_udata = userdata;
	return 0;
}

const char *term_getline(Terminal *term, const char *prefix) {
	return term_getline_inner(term, prefix, 0);
}
const char *term_password(Terminal *term, const char *prefix) {
	return term_getline_inner(term, prefix, 1);
}
int term_vprintf(Terminal *term, const char *format, va_list args) {
	int i = 0, rc = 0, pos_bak = 0;

	if (term == NULL) {
		rc = vprintf(format, args);
		goto func_end;
	}
	if (term->event == E_EVENT_EXEC) {
		rc = term_vprintf_inner(term, format, args); /* between command exec, just print */
		goto func_end;
	}
	pos_bak = term->pos;
	term_cursor_move(term, -(term->num + strlen(term->prompt) + 1), 0);
	for (i = 0; i < term->num + strlen(term->prompt) + 1; i++) {
		term_printf_inner(term, " ");
	}
	term_cursor_move(term, -(term->num + strlen(term->prompt) + 1), 0);
	rc = term_vprintf_inner(term, format, args);
	term_print_prompt(term); /* will set pos = 0 */
	term_refresh(term, pos_bak, term->num, 0); /* recover input and pos */
func_end:
	return rc;
}
int term_printf(Terminal *term, const char *format, ...) {
	int rc;
	va_list args;

	va_start(args, format);
	rc = term_vprintf(term, format, args);
	va_end(args);
	return rc;
}

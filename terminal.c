#include <stdio.h>
#include <stdlib.h>
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

#ifdef WATCH_RAM
#include "tt_malloc_debug.h"
#define MY_MALLOC(x) my_malloc((x), __FILE__, __LINE__)
#define MY_FREE(x) my_free((x), __FILE__, __LINE__)
#define MY_REALLOC(x, y) my_realloc((x), (y), __FILE__, __LINE__)
#else
#define MY_MALLOC(x) malloc((x))
#define MY_FREE(x) free((x))
#define MY_REALLOC(x, y) realloc((x), (y))
#endif

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

int isdelimiter(char ch) {
	int i = 0;
	const char *target = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

	for (i = 0; target[i] != '\0'; i++) {
		if (target[i] == ch) {
			return 1;
		}
	}
	return 0;
}

int term_vprintf(TERMINAL *term, const char *format, va_list args) {
	int ret = -1;

	if ((ret = tt_buffer_vprintf(&(term->tempbuf), format, args)) < 0) {
		ret = -1;
		goto exit;
	}
exit:
	return ret;
}

int term_printf(TERMINAL *term, const char *format, ...) {
	int rc;
	va_list args;

	va_start(args, format);
	rc = term_vprintf(term, format, args);
	va_end(args);
	/* TODO should split line by line and output as paging */
	term->write(term->tempbuf.content, term->tempbuf.used);
	term->tempbuf.used = 0;
#if 0
	for (line_tail = line_head = term->tempbuf.content; ;) {
		for (line_tail = line_head; *line_tail != '\0' && *line_tail != '\r' && *line_tail != '\n'; line_tail++);
		if ((*line_tail == '\r' && *(line_tail + 1) == '\n') || *line_tail == '\n') {
		}
		if (*line_tail == '\0') {
			break;
		}
	}
#endif
	return rc;
}

int term_command_write(TERMINAL *term, const void *content, size_t count) {
	return tt_buffer_write(&(term->line_command), content, count);
}

int term_command_vprintf(TERMINAL *term, const char *format, va_list args) {
	int ret = -1;

	if (0 != tt_buffer_vprintf(&(term->line_command), format, args)) {
		goto exit;
	}
	ret = 0;
exit:
	return ret;
}

int term_command_printf(TERMINAL *term, const char *format, ...) {
	int rc;
	va_list args;
	va_start(args, format);
	rc = term_command_vprintf(term, format, args);
	va_end(args);
	return rc;
}

void term_free_args(TERMINAL *term) {
	TERM_ARGS *p_cur = NULL, *p_next = NULL;

	for (p_cur = term->command_args; p_cur != NULL; p_cur = p_next) {
		p_next = p_cur->next;
		if (p_cur->content) {
			MY_FREE(p_cur->content);
		}
		MY_FREE(p_cur);
	}
	term->command_args = NULL;
}

extern int term_readline(TERMINAL *term);
/* term_split_args return 1 if need continue read content */
int term_split_args(TERMINAL *term) {
	int ret = 0;
	const char *start = NULL, *end = NULL;
	TERM_ARGS *p_new = NULL, *p_tail = NULL;
	TT_BUFFER command_buf, arg_buf;
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
			for (; *start == ' '; start++); /* move start for skip SPACE at linehead */
		}
		for (end = start; ; end++) { /* parse one argumeng */
			if ((*end == '"' || *end == '\'') && (end == start || *(end - 1) != '\\')) { /* found '"' or '\'' and no escape(\) */
				if (in_quot == '\0') { /* found quot */
					in_quot = *end;
				} else if (*end == in_quot) { /* end quot */
					in_quot = '\0';
				}
				continue;
			}
			if (*end == '\\' && *(end + 1) == '\0') { /* found '\\' at end of content */
				backslash_tail = 1;
				start = end + 1;
				eof = 1;
				break;
			}
			if (in_quot == '\0') {
				if (*end != ' ' && *end != '\0') {
					tt_buffer_write(&arg_buf, end, 1);
					continue;
				}
				if (arg_buf.used > 0) {
					p_new = (TERM_ARGS *)MY_MALLOC(sizeof(TERM_ARGS));
					if (p_new == NULL) {
						goto exit;
					}
					memset(p_new, 0x00, sizeof(TERM_ARGS));
					p_new->content = (char *)MY_MALLOC(arg_buf.used + 1);
					if (p_new->content == NULL) {
						goto exit;
					}
					memcpy(p_new->content, arg_buf.content, arg_buf.used);
					*(p_new->content + arg_buf.used) = '\0';
					tt_buffer_empty(&arg_buf);
					p_new->endword = (*end == '\0');
					if (term->command_args == NULL) {
						term->command_args = p_new;
					} else {
						p_new->prev = p_tail;
						p_tail->next = p_new; /* it's impossible that p_tail is NULL because term->command_args must be NULL at first loop */
						p_tail->endword = 0;
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
		ret = 1; /* return 1 means continue */
	}
	term->multiline = ret;
exit:
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

int term_getch(TERMINAL *term) {
	char key = 0;
#if defined(_WIN32)
	fflush (stdout);
	key = _getch();
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
	if (term->read(&key, 1) < 0) {
		perror("term->read()");
	}
	if (tcsetattr(STDIN_FILENO, TCSADRAIN, &old_term) < 0) {
		perror("tcsetattr");
	}
	// printf("%3d 0x%02x (%c)\n", key, key, isprint(key) ? key : ' ');
#endif
	return key;
}

void term_screen_get(TERMINAL *term, int *cols, int *rows) {
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

void term_cursor_move(TERMINAL *term, int col_off, int row_off) {
#if defined(_WIN32)
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo (GetStdHandle(STD_OUTPUT_HANDLE), &inf);
	inf.dwCursorPosition.Y += (SHORT)row_off;
	inf.dwCursorPosition.X += (SHORT)col_off;
	SetConsoleCursorPosition (GetStdHandle(STD_OUTPUT_HANDLE), inf.dwCursorPosition);
#else
	if (col_off > 0) {
		term_printf(term, "\e[%dC", col_off);
	} else if (col_off < 0) {
		term_printf(term, "\e[%dD", -col_off);
	}
	if (row_off > 0) {
		term_printf(term, "\e[%dB", row_off);
	} else if (row_off < 0) {
		term_printf(term, "\e[%dA", -row_off);
	}
#endif
}

void term_color_set(TERMINAL *term, unsigned int color) {
#if defined(_WIN32)
	CONSOLE_SCREEN_BUFFER_INFO info;
	static WORD dft_wAttributes = 0;
	WORD wAttributes = 0;
	if (!dft_wAttributes) {
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
		dft_wAttributes = info.wAttributes;
	}
	if (0 == (color & 0xff)) {
		wAttributes |= dft_wAttributes & (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	} else {
		wAttributes |= (color >= TERM_FGCOLOR_BRIGHT_BLACK) ? FOREGROUND_INTENSITY : 0;
		switch (color & 0xff) {
			case TERM_FGCOLOR_BRIGHT_RED:  	wAttributes |= FOREGROUND_RED;	break;
			case TERM_FGCOLOR_BRIGHT_GREEN:  	wAttributes |= FOREGROUND_GREEN;break;
			case TERM_FGCOLOR_BRIGHT_BLUE:  	wAttributes |= FOREGROUND_BLUE;	break;
			case TERM_FGCOLOR_BRIGHT_YELLOW:  wAttributes |= FOREGROUND_RED | FOREGROUND_GREEN;	break;
			case TERM_FGCOLOR_BRIGHT_MAGENTA: wAttributes |= FOREGROUND_RED | FOREGROUND_BLUE;	break;
			case TERM_FGCOLOR_BRIGHT_CYAN:	wAttributes |= FOREGROUND_GREEN | FOREGROUND_BLUE;	break;
			case TERM_FGCOLOR_BRIGHT_WHITE:   wAttributes |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;break;
		}
	}
	if (TERM_BGCOLOR_DEFAULT == (color & 0xff00)) {
		wAttributes |= dft_wAttributes & (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY);
	} else {
		wAttributes |= (color >= TERM_BGCOLOR_BRIGHT_BLACK) ? BACKGROUND_INTENSITY : 0;
		switch ((color & 0xff00)) {
			case TERM_BGCOLOR_BRIGHT_RED:  	wAttributes |= BACKGROUND_RED;	break;
			case TERM_BGCOLOR_BRIGHT_GREEN:  	wAttributes |= BACKGROUND_GREEN;break;
			case TERM_BGCOLOR_BRIGHT_BLUE:  	wAttributes |= BACKGROUND_BLUE;	break;
			case TERM_BGCOLOR_BRIGHT_YELLOW:  wAttributes |= BACKGROUND_RED | BACKGROUND_GREEN;	break;
			case TERM_BGCOLOR_BRIGHT_MAGENTA: wAttributes |= BACKGROUND_RED | BACKGROUND_BLUE;	break;
			case TERM_BGCOLOR_BRIGHT_CYAN:	wAttributes |= BACKGROUND_GREEN | BACKGROUND_BLUE;	break;
			case TERM_BGCOLOR_BRIGHT_WHITE:   wAttributes |= BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;break;
		}
	}
	if (color & TERM_STYLE_UNDERSCORE)
		{ wAttributes |= COMMON_LVB_UNDERSCORE; }
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), wAttributes);
#else
	term_printf(term, "\033[0m");
	if (color & 0xff) {
		term_printf(term, "\033[%dm", color & 0xff);
	}
	if (color & 0xff00) {
		term_printf(term, "\033[%dm", (color & 0xff00) >> 8);
	}
	if (color & TERM_STYLE_BOLD) {
		term_printf(term, "\033[1m");
	}
	if (color & TERM_STYLE_UNDERSCORE) {
		term_printf(term, "\033[4m");
	}
	if (color & TERM_STYLE_BLINKING) {
		term_printf(term, "\033[5m");
	}
	if (color & TERM_STYLE_INVERSE) {
		term_printf(term, "\033[7m");
	}
#endif
}

int term_prompt_set(TERMINAL *term, const char *prompt) {
	int ret = -1;
	if (term->prompt != NULL) {
		MY_FREE(term->prompt);
		term->prompt = NULL;
	}
	if (prompt == NULL) {
		term->prompt = (char *)MY_MALLOC(1);
		if (term->prompt == NULL) {
			goto exit;
		}
		term->prompt[0] = '\0';
	} else {
		term->prompt = (char *)MY_MALLOC(strlen(prompt) + 1);
		if (term->prompt == NULL) {
			goto exit;
		}
		strcpy(term->prompt, prompt);
	}
	ret = 0;
exit:
	return ret;
}

void term_prompt_color_set(TERMINAL *term, unsigned int color) {
	term->prompt_color = color;
}

void term_prompt_userdata_set(TERMINAL *term, void *userdata) {
	term->userdata = userdata;
}

void term_print_prompt(TERMINAL *term) {
	if (!term->multiline) {
		term_color_set(term, term->prompt_color);
		term_printf(term, term->prompt);
		term_printf(term, " ");
		term_color_set(term, TERM_COLOR_DEFAULT);
	} else {
		term_printf(term, "> ");
	}
	term->pos = 0;
}

ssize_t read_std(void *buf, size_t count) {
	return read(STDIN_FILENO, buf, count);
}

ssize_t write_std(const void *buf, size_t count) {
	return write(STDOUT_FILENO, buf, count);
}

int term_init(TERMINAL *term, const char *prompt, int (*cb)(TERMINAL *term)) {
	int not_support = 0;
	char *term_env = NULL;

	if (!isatty(STDIN_FILENO)) {  // input is not from a terminal
		not_support = 1;
	} else {
		term_env = getenv("TERM");
		if (NULL != term_env) {
			if (!strcasecmp(term_env, "dumb") || !strcasecmp(term_env, "cons25") ||  !strcasecmp(term_env, "emacs")) {
				not_support = 1;
			}
		}
	}
	if (not_support) {
		printf("not support\n");
		return -1;
	}

	memset(term, 0x00, sizeof(TERMINAL));
	tt_buffer_init(&(term->line_command));
	tt_buffer_swapto_malloced(&(term->line_command), 0); /* avoid term->line_command->content is null */
	tt_buffer_init(&(term->tempbuf));
	tt_buffer_init(&(term->prefix));
	tt_buffer_swapto_malloced(&(term->prefix), 0); /* avoid term->frefix->content is null */
	if (0 != term_prompt_set(term, prompt)) {
		return -1;
	}
	term->read = read_std;
	term->write = write_std;
	term->event_cb = cb;
	return 0;
}

void term_event_bind(TERMINAL *term, int (*cb)(TERMINAL *term)) {
	term->event_cb = cb;
}

void history_free(struct ST_TERM_STRINGS **p_head) {
	struct ST_TERM_STRINGS *p_cur = NULL, *p_prev = NULL;

	for (p_cur = (*p_head); p_cur != NULL; p_cur = p_prev) {
		p_prev = p_cur->prev;
		if (p_cur->content != NULL) {
			MY_FREE(p_cur->content);
		}
		MY_FREE(p_cur);
	}
	*p_head = NULL;
}

void wordhelp_free(struct ST_TERM_WORDHELP **p_head) {
	struct ST_TERM_WORDHELP *p_cur = NULL, *p_next = NULL;

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

void term_free(TERMINAL *term) {
	if (term->prompt != NULL) {
		MY_FREE(term->prompt);
	}
	tt_buffer_free(&(term->prefix));
	tt_buffer_free(&(term->line_command));
	tt_buffer_free(&(term->tempbuf));
	history_free(&(term->history));
	term_free_args(term);
	wordhelp_free(&(term->complete));
	wordhelp_free(&(term->hints));
	memset(term, 0x00, sizeof(TERMINAL));
}

int term_getkey(TERMINAL *term) {
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

void term_refresh(TERMINAL *term, int pos, int num, int chg_pos) {
	int i = 0, ret = 0, prompt_len = 0, pos_row = 0, pos_col = 0;;
	int rows = 0, cols = 0;

	term_screen_get(term, &cols, &rows);
	prompt_len = strlen(term->prompt) + 1; /* 1: will add space after prompt */

	if (chg_pos >= 0) { /* update changes */
		term->line_command.content[num] = '\0';
		pos_row = (chg_pos + prompt_len) / cols - (term->pos + prompt_len) / cols;
		pos_col = (chg_pos + prompt_len) % cols - (term->pos + prompt_len) % cols;
		term_cursor_move(term, pos_col, pos_row);
		if (term->mask) {
			for (i = 0; i <  (int)strlen((char *)(term->line_command.content) + chg_pos); i++) {
				term_printf(term, "*");
			}
			ret = i;
		} else {
			ret = term_printf(term, (char *)(term->line_command.content) + chg_pos);
		}
		if (ret < 0) {
			printf("term_printf error\n");
			return;
		}
		chg_pos += ret;
		for (i = 0; i < term->num - num; i++) {
			chg_pos += 1;
			term_printf(term, " ");
		}
		pos_row = (pos + prompt_len) / cols - (chg_pos + prompt_len) / cols;
		pos_col = (pos + prompt_len) % cols - (chg_pos + prompt_len) % cols;
		term_cursor_move(term, pos_col, pos_row);
	} else { /* move cursor */
		pos_row = (pos + prompt_len) / cols - (term->pos + prompt_len) / cols;
		pos_col = (pos + prompt_len) % cols - (term->pos + prompt_len) % cols;
		term_cursor_move(term, pos_col, pos_row);
	}
	term->pos = pos;
	term->num = num;
	term->line_command.used = num;
}

void term_wordhelp_free(struct ST_TERM_WORDHELP **wordhelp) {
	struct ST_TERM_WORDHELP *p_next = NULL, *p_cur = NULL;

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

void term_wordhelp_add(struct ST_TERM_WORDHELP **wordhelp, const char *word, const char *help) {
	int ret = -1;
	struct ST_TERM_WORDHELP *p_new = NULL, *p_cur = NULL;

	p_new = (struct ST_TERM_WORDHELP *)MY_MALLOC(sizeof(struct ST_TERM_WORDHELP));
	if (p_new == NULL) {
		goto exit;
	}
	memset(p_new, 0x00, sizeof(struct ST_TERM_WORDHELP));
	if (word == NULL) { /* word must not be NULL */
		goto exit;
	}
	p_new->word = (char *)MY_MALLOC(strlen(word) + 1);
	if (p_new->word == NULL) {
		goto exit;
	}
	strcpy(p_new->word, word);
	if (help != NULL) {
		p_new->help = (char *)MY_MALLOC(strlen(help) + 1);
		if (p_new->help == NULL) {
			goto exit;
		}
		strcpy(p_new->help, help);
	}
	if (*wordhelp == NULL) {
		*wordhelp = p_new;
	} else {
		for (p_cur = *wordhelp; p_cur->next != NULL; p_cur = p_cur->next);
		p_cur->next = p_new;
	}
	ret = 0;
exit:
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

void term_complete_free(TERMINAL *term) {
	term_wordhelp_free(&(term->complete));
}

void term_complete_add(TERMINAL *term, TERM_ARGS *p_arg, const char *word, const char *help) {
	term_wordhelp_add(&(term->complete), word, help);
}

void term_hints_free(TERMINAL *term) {
	term_wordhelp_free(&(term->hints));
}

void term_hints_add(TERMINAL *term, TERM_ARGS *p_arg, const char *word, const char *help) {
	if (p_arg == NULL || p_arg->endword) {
		term_wordhelp_add(&(term->hints), word, help);
	}
}

/* TODO for argument around by '"' or '\'' */
void term_history_add(TERMINAL *term) {
	int ret = -1, i = 0;
	size_t len = 0;
	TERM_ARGS *p_arg = NULL;
	TERM_HISTORY *p_new = NULL, *p_his = NULL, *p_prev = NULL;

	if (term->command_args == NULL) {
		goto exit;
	}
	len = 0;
	for (p_arg = term->command_args; p_arg != NULL; p_arg = p_arg->next) {
		len += strlen(p_arg->content) + 1;
	}
	p_new = (TERM_HISTORY *)MY_MALLOC(sizeof(TERM_HISTORY));
	if (p_new == NULL) {
		goto exit;
	}
	memset(p_new, 0x00, sizeof(TERM_HISTORY));
	p_new->content = (char *)MY_MALLOC(len);
	if (p_new->content == NULL) {
		goto exit;
	}
	*(p_new->content) = '\0';
	for (p_arg = term->command_args; p_arg != NULL; p_arg = p_arg->next) {
		if (*(p_new->content) != '\0') {
			strcat(p_new->content, " ");
		}
		strcat(p_new->content, p_arg->content);
	}
	if (term->history != NULL) {
		term->history->next = p_new;
	}
	p_new->prev = term->history;
	term->history = p_new;
	if (HISTORY_LENGTH) {
		for (i = 0, p_his = term->history; p_his != NULL; p_his = p_his->prev) { /* move to head */
			i++;
			if (i > HISTORY_LENGTH) {
				p_his->next->prev = NULL;
				break;
			}
		}
		for (; p_his != NULL; p_his = p_prev) {
			p_prev = p_his->prev;
			if (p_his->content != NULL) {
				MY_FREE(p_his->content);
			}
			MY_FREE(p_his);
		}
	}
	ret = 0;
exit:
	if (ret != 0) {
		if (p_new != NULL) {
			if (p_new->content != NULL) {
				MY_FREE(p_new->content);
			}
			MY_FREE(p_new);
		}
	}
	return;
}

void term_completion(TERMINAL *term) {
	int comman_len = 0, overlay = 0, start_pos = 0, end_pos = 0, has_change = 0;
	int i = 0, word_width = 0, with_help = 0, rows = 0, cols = 0, words_len = 0, word_num = 0;
	TERM_COMPLETE *p_cur = NULL;
	TERM_ARGS *p_lastarg = NULL;

	if (term->event_cb == NULL) {
		return;
	}
	term_split_args(term);
	/* find completion and help info */
	term->arg_prev = term->arg_cur = NULL;
	term->arg_next = term->command_args;
	term->event = E_EVENT_COMPLETE;
	term->event_cb(term);
	term->event = E_EVENT_NONE;

	if (term->complete != NULL) {
		comman_len = (int)strlen(term->complete->word);
		// find common string for auto complete
		for (p_cur = term->complete->next; (p_cur != NULL) && (comman_len > 0); p_cur = p_cur->next) {
			while ((comman_len > 0) && strncasecmp(term->complete->word, p_cur->word, comman_len)) {
				comman_len--;
			}
		}
		if (comman_len > 0) {
			overlay = 0;
			p_lastarg = NULL;
			if (term->command_args != NULL) {
				for (p_lastarg = term->command_args; p_lastarg->next != NULL; p_lastarg = p_lastarg->next);
			}
			if (p_lastarg && p_lastarg->endword && term->command_args != NULL) {
				overlay = strlen(p_lastarg->content);
			}
			start_pos = term->num - overlay;
			end_pos = start_pos + comman_len;
			if (comman_len > overlay) {
				/* malloc for complete, command_len - overlay is the size that need expand */
				tt_buffer_swapto_malloced(&(term->line_command), comman_len - overlay);
			}
			if (memcmp(term->line_command.content + start_pos, term->complete->word, comman_len)) {
				memcpy(term->line_command.content + start_pos, term->complete->word, comman_len);
				has_change = 1;
			}
			term->line_command.used += comman_len - overlay;
			*(term->line_command.content + end_pos) = '\0';
			if (term->complete->next == NULL) { /* only one match, add SPACE at the end of word */
				tt_buffer_swapto_malloced(&(term->line_command), 1); /* malloc for complete SPACE */
				strcat((char *)(term->line_command.content), " ");
				end_pos += 1;
				term->line_command.used += 1;
				has_change = 1;
			}
			// printf("term->pos %d, start_pos %d, end_pos %d\n", term->pos, start_pos, end_pos);
			term_refresh(term, end_pos, end_pos, start_pos);
		}
	}

	if (has_change == 0) {
		with_help = 0;
		word_width = 0;
		for (p_cur = term->complete; p_cur != NULL; p_cur = p_cur->next) {
			if (p_cur->help != NULL && p_cur->help[0] != '\0') {
				with_help = 1;
			}
			if ((int)strlen(p_cur->word) > word_width) {
				word_width = strlen(p_cur->word);
			}
		}
		for (p_cur = term->hints; p_cur != NULL; p_cur = p_cur->next) {
			if (p_cur->help != NULL && p_cur->help[0] != '\0') {
				with_help = 1;
			}
			if ((int)strlen(p_cur->word) > word_width) {
				word_width = strlen(p_cur->word);
			}
		}
		if (word_width > 0) {
			term_printf(term, "\n");
			if (with_help) { /* print word and help line by line */
				for (p_cur = term->complete; p_cur != NULL; p_cur = p_cur->next) {
					term_color_set(term, TERM_STYLE_BOLD);
					term_printf(term, "%s", p_cur->word);
					term_color_set(term, TERM_COLOR_DEFAULT);
					if (p_cur->help != NULL) {
						term_printf(term, "%*s	 %s\n", word_width - strlen(p_cur->word), "", p_cur->help);
					} else {
						term_printf(term, "\n"); /* show word only if help is NULL */
					}
				}
				for (p_cur = term->hints; p_cur != NULL; p_cur = p_cur->next) {
					term_color_set(term, TERM_STYLE_UNDERSCORE);
					term_printf(term, "%s", p_cur->word);
					term_color_set(term, TERM_COLOR_DEFAULT);
					if (p_cur->help != NULL) {
						term_printf(term, "%*s	 %s\n", word_width - strlen(p_cur->word), "", p_cur->help);
					} else {
						term_printf(term, "\n");
					}
				}
			} else { /* only show words */
				term_screen_get(term, &cols, &rows);
				words_len = 0;
				i = 0;
				for (p_cur = term->complete; p_cur != NULL; p_cur = p_cur->next, i++) {
					if (i != 0) {
						words_len += 2;
					}
					words_len += strlen(p_cur->word);
				}
				for (p_cur = term->hints; p_cur != NULL; p_cur = p_cur->next, i++) {
					if (i != 0) {
						words_len += 2;
					}
					words_len += strlen(p_cur->word);
				}
				if (words_len <= cols) {
					i = 0;
					for (p_cur = term->complete; p_cur != NULL; p_cur = p_cur->next, i++) {
						if (i != 0) {
							term_printf(term, "  ");
						}
						term_color_set(term, TERM_STYLE_BOLD);
						term_printf(term, "%s", p_cur->word);
						term_color_set(term, TERM_COLOR_DEFAULT);
					}
					for (p_cur = term->hints; p_cur != NULL; p_cur = p_cur->next, i++) {
						if (i != 0) {
							term_printf(term, "  ");
						}
						term_color_set(term, TERM_STYLE_UNDERSCORE);
						term_printf(term, "%s", p_cur->word);
						term_color_set(term, TERM_COLOR_DEFAULT);
					}
					term_printf(term, "\n");
				} else { /* print word as a table */
					word_num = ((cols - word_width) / (word_width + 2)) + 1;
					i = 0;
					for (p_cur = term->complete; p_cur != NULL; p_cur = p_cur->next, i++) {
						if (i % word_num == 0) {
							term_printf(term, "%s", i ? "\n" : "");
						} else {
							term_printf(term, "  ");
						}
						term_color_set(term, TERM_STYLE_BOLD);
						term_printf(term, "%s", p_cur->word);
						term_color_set(term, TERM_COLOR_DEFAULT);
						term_printf(term, "%*s", word_width - strlen(p_cur->word), "");
					}
					for (p_cur = term->hints; p_cur != NULL; p_cur = p_cur->next, i++) {
						if (i % word_num == 0) {
							term_printf(term, "%s", i ? "\n" : "");
						} else {
							term_printf(term, "  ");
						}
						term_color_set(term, TERM_STYLE_UNDERSCORE);
						term_printf(term, "%s", p_cur->word);
						term_color_set(term, TERM_COLOR_DEFAULT);
						term_printf(term, "%*s", word_width - strlen(p_cur->word), "");
					}
					term_printf(term, "\n");
				}
			}
			term_print_prompt(term);
			// printf("term->pos %d, line_command %d\n", term->pos, strlen((char *)(term->line_command.content)));
			term_refresh(term, strlen((char *)(term->line_command.content)), strlen((char *)(term->line_command.content)), 0);
		}
	}
	term_complete_free(term);
	term_hints_free(term);
}

void term_execute(TERMINAL *term) {
	int i = 0, ret = 0;
	TERM_ARGS *p_arg = NULL;

	term_history_add(term);
	term->event = E_EVENT_EXEC;
	term->arg_prev = term->arg_cur = NULL;
	term->arg_next = term->command_args;
	if (term->event_cb != NULL) {
		ret = term->event_cb(term);
		if (ret != 0) {
			term_printf(term, "command not found.\n");
#if 0
			for (i = 0, p_arg = term->command_args; p_arg != NULL; p_arg = p_arg->next, i++) {
				term_printf(term, "\targ[%d]: \"%s\"\n", i, p_arg->content);
			}
#endif
			term->event = E_EVENT_NONE;
		}
	}
	term_complete_free(term);
	term_hints_free(term);
 }

int argument(TERMINAL *term, const char *word, const char *help) {
	int result = 0;
	TERM_ARGS *p_arg = term->arg_cur;

	if (term->event == E_EVENT_COMPLETE || term->event == E_EVENT_HELP) {
		if (p_arg == NULL || p_arg->endword) {
			term_hints_add(term, p_arg, word, help);
		}
		if (p_arg != NULL && !p_arg->endword) {
			result |= MATCH_ACT_FORWARD;
		}
	}
	if (term->event == E_EVENT_EXEC) {
		if (p_arg != NULL) {
			if (p_arg->next == NULL) {
				result |= MATCH_ACT_EXEC;
				term->event = E_EVENT_NONE;
			}
			if (!p_arg->endword) {
				result |= MATCH_ACT_FORWARD;
			}
		}
	}
	return result;
}

int keyword(TERMINAL *term, const char *target, const char *word, const char *help) {
	int result = 0;
	TERM_ARGS *p_arg = term->arg_cur;

	if (p_arg == NULL || *(p_arg->content) == '\0') {
		result |= MATCH_EMPTY;
	} else {
		if (0 == strncasecmp(p_arg->content, target, strlen(p_arg->content))) {
			result |= MATCH_PART;
			if (0 == strcasecmp(p_arg->content, target)) {
				result |= MATCH_ENTIRE;
			}
		}
	}
	if (term->event == E_EVENT_COMPLETE || term->event == E_EVENT_HELP) {
		if (result & MATCH_ENTIRE) {
			if (!p_arg->endword) { /* line_command end with SPACE */
				result |= MATCH_ACT_FORWARD;
			} else {
				result |= MATCH_ACT_EXPAND; /* need expand SPACE */
			}
		} else if (result/* equal: result & (MATCH_EMPTY | MATCH_PART) */) {
			result |= MATCH_ACT_EXPAND; /* arg is part match or empty, need complete */
		}
	} else if (term->event == E_EVENT_EXEC) {
		if (result & MATCH_ENTIRE) {
			if (p_arg->next == NULL) {
				result |= MATCH_ACT_EXEC;
				term->event = E_EVENT_NONE;
			}
			if (!p_arg->endword) { /* line_command end with SPACE */
				result |= MATCH_ACT_FORWARD;
			}
		}
	}
	if ((result & MATCH_ACT_EXPAND) && (!p_arg || (p_arg->endword && !p_arg->next))) {
		term_complete_add(term, p_arg, word, help);
	}
	return result;
}

void term_exit(TERMINAL *term) {
	term->exit_flag = 1;
}

int term_readline(TERMINAL *term) {
	int key = 0;
	int need_exit = 0, need_exec = 0, length = 0, new_pos = 0;

	while (1) {/* loop once every command */
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
						term->history_cursor = (term->history_cursor == NULL) ? term->history : term->history_cursor->prev;
					} else {
						if (term->history_cursor != NULL) {
							term->history_cursor = term->history_cursor->next;
						}
					}
					if (term->history_cursor != NULL) {
						term->line_command.used = 0;
						term_command_write(term, term->history_cursor->content, strlen(term->history_cursor->content));
						length = term->line_command.used;
						term_refresh(term, length, length, 0);
					} else {
						term_refresh(term, 0, 0, 0);
					}
					break;

				/* complete */
				case KEY_TAB:		// Autocomplete (same with KEY_CTRL('I'))
					term_completion(term);
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
						term_printf(term, "exit because press Ctrl+D\n");
						need_exit = -1;
					}
					break;
				case KEY_CR:
				case KEY_LF:
					term_printf(term, "\n");
					if (term->line_command.used > 0) {
						if (term_split_args(term) == 0) {
							need_exec = 1;
						} else {
							term_print_prompt(term);
							term_refresh(term, 0, 0, 0);
						}
					} else {
						if (term->multiline && term_split_args(term) == 0) { /* function need return while in multiline mode */
							need_exec = 1;
						} else {
							term_print_prompt(term);
							term_refresh(term, 0, 0, 0);
						}
					}
					term->history_cursor = NULL;
					break;
				case KEY_CTRL('C'):
				case KEY_CTRL('G'):
					if (term->multiline) {
						term->multiline = 0;
						term_printf(term, "%s\n", (key == KEY_CTRL('C')) ? "^C" : "^G");
						tt_buffer_empty(&(term->prefix));
						term_print_prompt(term);
						term_refresh(term, 0, 0, 0);
					} else {
						if (term->num > 0) {
							term_printf(term, "%s\n", (key == KEY_CTRL('C')) ? "^C" : "^G");
							term_print_prompt(term);
							term_refresh(term, 0, 0, 0);
						} else {
							term_printf(term, "exit because press Ctrl+%s\n", (key == KEY_CTRL('C')) ? "C" : "G");
							need_exit = -1;
						}
					}
					break;
				case KEY_CTRL('Z'):
#if defined(_WIN32)
					term_printf(term, "exit because press Ctrl+Z\n");
					need_exit = -1;
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
			}
			if (need_exec || need_exit) {
				break;
			}
		}
		if (need_exec) {
			term_execute(term);
			if (term->exit_flag) {
				return -1;
			}
			need_exec = 0;
			break; /* delete this break if no need exit func every time */
		}
		if (need_exit) {
			break;
		}
	}
	if (need_exit < 0) { /* Ctrl+C */
		term_free_args(term);
		return -1;
	}
	return 0;
}
TERM_ARGS *term_argpush(TERMINAL *term) {
	term->arg_prev = term->arg_cur;
	term->arg_cur = term->arg_next;
	if (term->arg_cur) {
		term->arg_next = term->arg_cur->next;
	} else {
		term->arg_next = NULL;
	}
	return term->arg_cur;
}
TERM_ARGS *term_argpop(TERMINAL *term) {
	term->arg_next = term->arg_cur;
	term->arg_cur = term->arg_prev;
	if (term->arg_cur) {
		term->arg_prev = term->arg_cur->prev;
	} else {
		term->arg_prev = NULL;
	}
	return term->arg_cur;
}

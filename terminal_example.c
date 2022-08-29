#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terminal.h"

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

int term_event_dispatch(TERMINAL *term) {
	TERMINAL pwd_term;
	TERM_ARGS *cur = NULL;

	cur = term_argpush(term);
	if (keyword(term, "opencam", "opencam", "Open camera") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (keyword(term, "gige", "GigE", NULL) & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if ((keyword(term, "c4-2f-90-f8-67-d3", "C4-2F-90-F8-67-D3", NULL) & MATCH_ACT_EXEC) \
					|| (keyword(term, "c4-2f-a0-f8-67-d3", "C4-2F-A0-F8-67-D3", NULL) & MATCH_ACT_EXEC) \
					|| (argument(term, "mac", "mac address of camera") & MATCH_ACT_EXEC)) {
				printf("callback open_camera(\"%s\", CAM_TYPE_GIGE, cam_event_dispatcher);\n", cur->content);
				return 0;
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "usb", "USB", NULL) & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if ((keyword(term, "/dev/video0", "/dev/video0", NULL) & MATCH_ACT_EXEC) \
					|| (keyword(term, "/dev/video2", "/dev/video2", NULL) & MATCH_ACT_EXEC) \
					|| (argument(term, "path", "path of USB camera") & MATCH_ACT_EXEC)) {
				printf("callback open_camera(\"%s\", CAM_TYPE_USB, cam_event_dispatcher);\n", cur->content);
				return 0;
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "network", "network", NULL) & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if (argument(term, "url", "url of network camera, ex: rtsp://uname:pwd@abc.com/example.mov") & MATCH_ACT_EXEC) {
				printf("callback open_camera(\"%s\", CAM_TYPE_USB, cam_event_dispatcher);\n", cur->content);
				return 0;
			}
			cur = term_argpop(term);
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "closecam", "closecam", "Close camera") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (argument(term, "camera_id", NULL) & MATCH_ACT_EXEC) {
			printf("callback close_camera(%d);\n", atoi(cur->content));
			term->event = E_EVENT_NONE;
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "set", "set", "set action") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (keyword(term, "prompt", "prompt", "set prompt") & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if (argument(term, "content", "prompt content") & MATCH_ACT_EXEC) {
				term_init(&pwd_term, "password: ", NULL);
				pwd_term.mask = 1;
				if (term_readline(&pwd_term) == 0) {
					if (0 == strcmp(pwd_term.command_args->content, "123")) {
						term_prompt_set(term, cur->content);
					} else {
						printf("invalid password \"%s\", mismatch with \"123\"\n", pwd_term.command_args->content);
					}
				} else {
					printf("Cancel\n");
				}
				term_free(&pwd_term);
				return 0;
			}
			cur = term_argpop(term);
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "show", "show", "Show Information") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (keyword(term, "camera", "camera", NULL) & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if (argument(term, "camera_id", NULL) & MATCH_ACT_FORWARD) {
				cur = term_argpush(term);
				if (keyword(term, "step", "step", NULL) & MATCH_ACT_FORWARD) {
					cur = term_argpush(term);
					if (argument(term, "step number", NULL) & MATCH_ACT_EXEC) {
						printf("callback call show camera\n");
						return 0;
					}
					cur = term_argpop(term);
				}
				cur = term_argpop(term);
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "aaa", "aaa", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "aaaaaa", "aaaaaa", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "bb", "bb", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "c", "c", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "dddddd", "dddddd", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "ee", "ee", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "ff", "ff", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "ggggg", "ggggg", NULL) & MATCH_ACT_EXEC) { return 0; }
		if (keyword(term, "hhhhh", "hhhhh", NULL) & MATCH_ACT_EXEC) { return 0; }
		cur = term_argpop(term);
	}
	if (keyword(term, "exit", "exit", NULL) & MATCH_ACT_EXEC) {
		term_exit(term);
		return 0;
	}
	if (keyword(term, "quit", "quit", NULL) & MATCH_ACT_EXEC) {
		term_exit(term);
		return 0;
	}
	return -1;
}

int main() {
	TERMINAL term;

#if 1
	term_init(&term, "test$", term_event_dispatch);
#else
	term_init(&term, "test$", NULL);
	term_event_bind(&term, term_event_dispatch);
#endif
	term_prompt_color_set(&term, TERM_FGCOLOR_BRIGHT_GREEN | TERM_STYLE_BOLD);
	term_prompt_userdata_set(&term, "hello");
	while (term_readline(&term) == 0);
	term_free(&term);
#ifdef WATCH_RAM
	show_ram(1);
#endif
}


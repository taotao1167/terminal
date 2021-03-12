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

void term_event_dispatch(TERMINAL *term) {
	TERMINAL pwd_term;
	TERM_ARGS *p_arg = NULL;

	p_arg = term->command_args;
	if (keyword(term, p_arg, "opencam", "opencam", "Open camera") & MATCH_ACT_FORWARD) {
		if (keyword(term, p_arg->next, "gige", "GigE", NULL) & MATCH_ACT_FORWARD) {
			keyword(term, p_arg->next->next, "c4-2f-90-f8-67-d3", "C4-2F-90-F8-67-D3", NULL);
			keyword(term, p_arg->next->next, "c4-2f-a0-f8-67-d3", "C4-2F-A0-F8-67-D3", NULL);
			if (argument(term, p_arg->next->next, "mac", "mac address of camera") & MATCH_ACT_EXEC) {
				printf("callback open_camera(\"%s\", CAM_TYPE_GIGE, cam_event_dispatcher);\n", p_arg->next->next->content);
				term->event = E_EVENT_NONE;
			}
		}
		if (keyword(term, p_arg->next, "usb", "USB", NULL) & MATCH_ACT_FORWARD) {
			keyword(term, p_arg->next->next, "/dev/video0", "/dev/video0", NULL);
			keyword(term, p_arg->next->next, "/dev/video2", "/dev/video2", NULL); 
			if (argument(term, p_arg->next->next, "path", "path of USB camera") & MATCH_ACT_EXEC) {
				printf("callback open_camera(\"%s\", CAM_TYPE_USB, cam_event_dispatcher);\n", p_arg->next->next->content);
				term->event = E_EVENT_NONE;
			}
		}
		if (keyword(term, p_arg->next, "network", "network", NULL) & MATCH_ACT_FORWARD) {
			if (argument(term, p_arg->next->next, "url", "url of network camera, ex: rtsp://uname:pwd@abc.com/example.mov") & MATCH_ACT_EXEC) {
				printf("callback open_camera(\"%s\", CAM_TYPE_USB, cam_event_dispatcher);\n", p_arg->next->next->content);
				term->event = E_EVENT_NONE;
			}
		}
	}
	if (keyword(term, p_arg, "closecam", "closecam", "Close camera") & MATCH_ACT_FORWARD) {
		if (argument(term, p_arg->next, "camera_id", NULL) & MATCH_ACT_EXEC) {
			printf("callback close_camera(%d);\n", atoi(p_arg->next->content));
			term->event = E_EVENT_NONE;
		}
	}
	if (keyword(term, p_arg, "set", "set", NULL) & MATCH_ACT_FORWARD) {
		if (keyword(term, p_arg->next, "prompt", "prompt", NULL) & MATCH_ACT_FORWARD) {
			if (argument(term, p_arg->next->next, "content", NULL) & MATCH_ACT_EXEC) {
				term_init(&pwd_term, "password: ", NULL);
				pwd_term.mask = 1;
				if (term_readline(&pwd_term) == 0) {
					if (0 == strcmp(pwd_term.command_args->content, "123")) {
						term_prompt_set(term, p_arg->next->next->content);
					} else {
						printf("invalid password \"%s\", different with \"123\"\n", pwd_term.command_args->content);
					}
				} else {
					printf("Cancel\n");
				}
				term_free(&pwd_term);
				term->event = E_EVENT_NONE;
			}
		}
	}
	if (keyword(term, p_arg, "show", "show", "Show Information") & MATCH_ACT_FORWARD) {
#ifdef WATCH_RAM
		if (keyword(term, p_arg->next, "memory", "memory", NULL) & MATCH_ACT_EXEC) {
			show_ram(1);
			term->event = E_EVENT_NONE;
		}
#endif
		if (keyword(term, p_arg->next, "camera", "camera", NULL) & MATCH_ACT_FORWARD) {
			if (argument(term, p_arg->next->next, "camera_id", NULL) & MATCH_ACT_FORWARD) {
				if (keyword(term, p_arg->next->next->next, "step", "step", NULL) & MATCH_ACT_FORWARD) {
					if (argument(term, p_arg->next->next->next->next, "step number", NULL) & MATCH_ACT_EXEC) {
						printf("callback call show camera\n");
						term->event = E_EVENT_NONE;
					}
				}
			}
		}
		if (keyword(term, p_arg->next, "aaa", "aaa", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "aaaaaa", "aaaaaa", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "bb", "bb", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "c", "c", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "dddddd", "dddddd", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "ee", "ee", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "ff", "ff", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "ggggg", "ggggg", NULL) & MATCH_ACT_EXEC) { }
		if (keyword(term, p_arg->next, "hhhhh", "hhhhh", NULL) & MATCH_ACT_EXEC) { }
	}
	if (keyword(term, p_arg, "exit", "exit", NULL) & MATCH_ACT_EXEC) {
		term_exit(term);
		term->event = E_EVENT_NONE;
	}
	return;
}

int main() {
	TERMINAL term;

#if 1
	term_init(&term, "kyland$ ", term_event_dispatch);
#else
	term_init(&term, "kyland$ ", NULL);
	term_event_bind(&term, term_event_dispatch);
#endif
	term_prompt_color_set(&term, TERM_FGCOLOR_BRIGHT_GREEN | TERM_STYLE_BOLD);
	while (term_readline(&term) == 0);
	term_free(&term);
#ifdef WATCH_RAM
	show_ram(1);
#endif
}


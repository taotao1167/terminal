#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "pi_list.h"
#include "terminal.h"
#include "PIMediaPlayer.h"
#if defined(_WIN32)
	#include <windows.h>
	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
#endif

extern int PIMC_SetAnalyzeCode(int type, int code);

enum MODE {
	MODE_NORMAL = 0,
	MODE_PLAYER
};

typedef struct PLAYER_STATE {
	int savedata; /* save yuv/rgb or not in videocallback */
	char *name;
	void *mp; /* return value of PIMediaPlayer_crate */
	char *url;
	void *window;
} PLAYER_STATE;

typedef struct WINDOW_STATE {
	char *name;
	int width;
	int height;
	void *hwnd;
} WINDOW_STATE;

typedef struct USERDATA {
	int mode;
	char *prompts[8];
	PLAYER_STATE *player_state;
	unsigned int win_thread_id;
} USERDATA;

PI_LIST g_players;
PI_LIST g_windows;

#if defined(_WIN32)
const char *picfmt_name[] = {"nv12", "inv", "inv", "argb", "rgb24", "i420"};
#else
const char *picfmt_name[] = {"nv12", "inv", "inv", "bgra", "rgb24", "i420"};
#endif
const char *suffix[] = {"yuv", "inv", "inv", "rgb", "rgb", "yuv"};

#if defined(_WIN32)
#define MSG_END_PROCESS (WM_USER + 1)
#define MSG_NEW_WINDOW  (WM_USER + 2)
#define MSG_MOVE_WINDOW (WM_USER + 3)
#define MSG_CLOSE_WINDOW (WM_USER + 4)
HINSTANCE hInstance = NULL;
HWND hwndMain = NULL;
LRESULT CALLBACK WindowProc(_In_ HWND hwnd, _In_ UINT uMsg, _In_  WPARAM wParam, _In_ LPARAM lParam) {
	PI_LISTE *entry = NULL;
	WINDOW_STATE *window = NULL;
	switch (uMsg) {
		case WM_CLOSE:
			// do nothing, printf("WM_CLOSE ignored\n");
			return 0;
		case MSG_CLOSE_WINDOW:
			printf("destroy window 0x%p\n", hwnd);
			DestroyWindow(hwnd);
			for (entry = g_windows.head; entry != NULL; entry = entry->next) {
				window = (WINDOW_STATE *)entry->payload;
				if (window->hwnd == hwnd) {
					pi_list_remove(&g_windows, entry);
					break;
				}
			}
			return 0;
		case WM_DESTROY:
			// do nothing, "PostQuitMessage(0);" will destroy all windows.
			return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
HWND create_window(const char *name, int width, int height) {
	HWND hwnd;
	hwnd = CreateWindow("ClassName1", name, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, hInstance, NULL);
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	return hwnd;
}
#endif

void exit_player_mode(TERMINAL *term) {
	((USERDATA *)(term->userdata))->mode = MODE_NORMAL;
	((USERDATA *)(term->userdata))->player_state = NULL;
	term_prompt_set(term, ((USERDATA *)(term->userdata))->prompts[0]);
	free(((USERDATA *)(term->userdata))->prompts[0]);
	((USERDATA *)(term->userdata))->prompts[0] = NULL;
}
static int videoCallback(void *opaque, void*frame, int *size, int64_t stamp, int picfmt, int width, int height, int index) {
	FILE *fout = NULL;
	char fname[64] = {0};

	if (((PLAYER_STATE *)opaque)->savedata) {
		sprintf(fname, "%p_%dx%d_%s.%s", ((PLAYER_STATE *)opaque)->mp, width, height, picfmt_name[picfmt], suffix[picfmt]);
		fout = fopen(fname, "ab");
		fwrite(frame, *size, 1, fout);
		fclose(fout);
	}
	// printf("%s %dx%d %s\n", __func__, width, height, picfmt_name[picfmt]);
	return 0;
}
static int videoCallbackEI(void *opaque, void*frame, int *size, int64_t stamp, int picfmt, int width, int height, int index, struct ExtraInfo *extrainfo) {
	FILE *fout = NULL;
	char fname[64] = {0};
	int64_t orig_timestamp = extrainfo->orig_timestamp;
	if (((PLAYER_STATE *)opaque)->savedata) {
		sprintf(fname, "%p.org_stamp", ((PLAYER_STATE *)opaque)->mp);
		fout = fopen(fname, "a");
		fprintf(fout, "%dx%d %s stamp:%" PRId64 ", orig_timestamp:%" PRId64 "\n", width, height, picfmt_name[picfmt], stamp, orig_timestamp);
		fclose(fout);
	}
	videoCallback(opaque, frame, size, stamp, picfmt, width, height, index); /* for save yuv/rgb */
	return 0;
}
void set_player_videocallback(USERDATA *userdata, const char *fmt, int savedata, int with_ei) {
	int64_t arg[5] = {0};
	int64_t picfmt = 0;
	PLAYER_STATE *player_state = userdata->player_state;

	player_state->savedata = savedata;
	if (!strcmp(fmt, "i420")) {
		picfmt = 5;
	} else if (!strcmp(fmt, "nv12")) {
		picfmt = 0;
	} else if (!strcmp(fmt, "argb")) {
		picfmt = 3;
	} else if (!strcmp(fmt, "rgb24")) {
		picfmt = 4;
	} else {
		picfmt = 5;
	}
	arg[0] = (int64_t)player_state;
	arg[1] = (int64_t)(with_ei ? videoCallbackEI : videoCallback);
	arg[2] = picfmt;
	arg[3] = 0;
	arg[4] = 0;
	PIMediaPlayer_set_player_op(player_state->mp, NULL, with_ei ? PIMEDIAPLAYER_OP_ID_SET_VIDEO_CALLBACK_EI : PIMEDIAPLAYER_OP_ID_SET_VIDEO_CALLBACK, 5, (void*)arg, 0, NULL);
	printf("set videocallback for %p success.\n", player_state->mp);
}
static void player_set(TERMINAL *term, PLAYER_STATE *player) {
	char *new_prompt[80] = {0};
	((USERDATA *)(term->userdata))->mode = MODE_PLAYER;
	((USERDATA *)(term->userdata))->player_state = player;
	((USERDATA *)(term->userdata))->prompts[0] = strdup(term->prompt); /* backup */
	sprintf(new_prompt, "Player(%s)$", player->name);
	term_prompt_set(term, new_prompt);
}
void player_create(TERMINAL *term, const char *name, const char *url, void *hwnd) {
	void *mp = NULL;
	PLAYER_STATE *player = NULL;

	mp = PIMediaPlayer_create((void *)(strncmp(url, "pzsp://", 7) ? 0 : 1), NULL);
	PIMediaPlayer_set_data_source(mp, NULL, url);
	PIMediaPlayer_set_hwnd(mp, hwnd);
	player = (PLAYER_STATE *)malloc(sizeof(PLAYER_STATE));
	memset(player, 0x00, sizeof(PLAYER_STATE));
	player->mp = mp;
	player->name = strdup(name);
	player->url = strdup(url);
	pi_list_push(&g_players, player);
	printf("player (0x%p) created\n", mp);
	player_set(term, player);
}
static void window_use(TERMINAL *term, PLAYER_STATE *player) {
	char *new_prompt[80] = {0};
	((USERDATA *)(term->userdata))->mode = MODE_PLAYER;
	((USERDATA *)(term->userdata))->player_state = player;
	((USERDATA *)(term->userdata))->prompts[0] = strdup(term->prompt); /* backup */
	sprintf(new_prompt, "Player(%s)$", player->name);
	term_prompt_set(term, new_prompt);
}
#if defined(_WIN32)
static int window_dispatch(TERMINAL *term) {
	TERM_ARGS *cur = NULL;
	PI_LISTE *entry = NULL;
	WINDOW_STATE *window = NULL;
	char argv1[32] = {0}, argv2[80] = {0};
	const char *name = NULL, *width = NULL, *height = NULL;

	if (keyword(term, "create", "create", "Create window") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (argument(term, "name", "Window name") & MATCH_ACT_FORWARD) {
			name = cur->content;
			cur = term_argpush(term);
			if (argument(term, "width", "Window width") & MATCH_ACT_FORWARD) {
				width = cur->content;
				cur = term_argpush(term);
				if (argument(term, "height", "Window height") & MATCH_ACT_EXEC) {
					height = cur->content;
					window = (WINDOW_STATE *)malloc(sizeof(WINDOW_STATE));
					window->name = strdup(name);
					window->width = atoi(width);
					window->height = atoi(height);
					PostThreadMessage(((USERDATA *)(term->userdata))->win_thread_id, MSG_NEW_WINDOW, (WPARAM)window, NULL);
					return 0;
				}
				cur = term_argpop(term);
			}
			cur = term_argpop(term);
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "close", "close", "Close window") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		for (entry = g_windows.head; entry != NULL; entry = entry->next) {
			window = (WINDOW_STATE *)entry->payload;
			sprintf(argv1, "%s", window->name);
			sprintf(argv2, "window %s %dx%d @ 0x%p", window->name, window->width, window->height, window->hwnd);
			if (keyword(term, argv1, argv1, argv2) & MATCH_ACT_EXEC) {
				SendMessageA(window->hwnd, MSG_CLOSE_WINDOW, 0, 0);
				return 0;
			}
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "size", "size", "Change window size") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		for (entry = g_windows.head; entry != NULL; entry = entry->next) {
			window = (WINDOW_STATE *)entry->payload;
			sprintf(argv1, "%s", window->name);
			sprintf(argv2, "window %s %dx%d @ 0x%p", window->name, window->width, window->height, window->hwnd);
			if (keyword(term, argv1, argv1, argv2) & MATCH_ACT_FORWARD) {
				cur = term_argpush(term);
				if (argument(term, "width", "Window width") & MATCH_ACT_FORWARD) {
					width = cur->content;
					cur = term_argpush(term);
					if (argument(term, "height", "Window height") & MATCH_ACT_EXEC) {
						height = cur->content;
						window->width = atoi(width);
						window->height = atoi(height);
						PostThreadMessage(((USERDATA *)(term->userdata))->win_thread_id, MSG_MOVE_WINDOW, (WPARAM)window, NULL);
						return 0;
					}
					cur = term_argpop(term);
				}
				cur = term_argpop(term);
			}
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "list", "list", "List all windows") & MATCH_ACT_EXEC) {
		for (entry = g_windows.head; entry != NULL; entry = entry->next) {
			window = (WINDOW_STATE *)entry->payload;
			printf("%s:\n", window->name);
			printf("\twidth: %d\n", window->width);
			printf("\theight: %d\n", window->height);
			printf("\thwnd: 0x%p\n", window->hwnd);
		}
		if (g_windows.head == NULL) {
			printf("empty\n");
		}
		return 0;
	}
	return -1;
}
#endif /* defined(_WIN32) */
static int player_dispatch(TERMINAL *term) {
	TERM_ARGS *cur = NULL;
	PI_LISTE *entry = NULL;
	char argv1[32] = {0}, argv2[80] = {0};
	PLAYER_STATE *player = NULL;
	WINDOW_STATE *window = NULL;
	void *mp = NULL;
	const char *url = NULL, *name = NULL;

	if (keyword(term, "set", "set", "Set player") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		for (entry = g_players.head; entry != NULL; entry = entry->next) {
			player = (PLAYER_STATE *)entry->payload;
			sprintf(argv1, "%s", player->name);
			sprintf(argv2, "player %s @ 0x%p", player->name, player->mp);
			if ((keyword(term, argv1, argv1, argv2) & MATCH_ACT_EXEC)) {
				mp = player->mp;
				player_set(term, player);
				return 0;
			}
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "create", "create", "Create player") & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (argument(term, "name", "Player name") & MATCH_ACT_FORWARD) {
			name = cur->content;
			cur = term_argpush(term);
			if (argument(term, "url", "ex: pzsp://xxx or D:/media/revenge.mp4") & MATCH_ACT_FORWARD) {
				url = cur->content;
				cur = term_argpush(term);
				if (keyword(term, "NULL", "NULL", "Create player with hwnd NULL") & MATCH_ACT_EXEC) {
					player_create(term, name, url, NULL);
					return 0;
				}
				for (entry = g_windows.head; entry != NULL; entry = entry->next) {
					window = (WINDOW_STATE *)entry->payload;
					sprintf(argv1, "%s", window->name);
					sprintf(argv2, "Create player with hwnd %s %dx%d @ 0x%p", window->name, window->width, window->height, window->hwnd);
					if (keyword(term, argv1, argv1, argv2) & MATCH_ACT_EXEC) {
						player_create(term, name, url, window->hwnd);
						return 0;
					}
				}
				cur = term_argpop(term);
			}
			cur = term_argpop(term);
		}
		cur = term_argpop(term);
	}
	if (keyword(term, "list", "list", "List all players") & MATCH_ACT_EXEC) {
		for (entry = g_players.head; entry != NULL; entry = entry->next) {
			player = (PLAYER_STATE *)entry->payload;
			printf("%s:\n", player->name);
			printf("\taddr:0x%p\n", player->mp);
			printf("\turl:%s\n", player->url);
		}
		if (g_players.head == NULL) {
			printf("empty\n");
		}
		return 0;
	}
	return -1;
}

static int log_dispatch(TERMINAL *term, TERM_ARGS *cur) {
	const char *type = NULL;
	int flush = 0;
	int level = 0;

	if ((keyword(term, "player", "player", NULL) & MATCH_ACT_FORWARD) \
			|| (keyword(term, "pslstreaming", "pslstreaming", NULL) & MATCH_ACT_FORWARD) \
			|| (keyword(term, "ptcp", "ptcp", NULL) & MATCH_ACT_FORWARD)) {
		type = cur->content;
		cur = term_argpush(term);
		if (argument(term, "flush", "1~128") & MATCH_ACT_FORWARD) {
			flush = atoi(cur->content);
			cur = term_argpush(term);
			if (argument(term, "level", "1~6") & MATCH_ACT_EXEC) {
				level = flush * 1000 + atoi(cur->content);
				if (!strcasecmp(type, "player")) {
					PIMC_SetAnalyzeCode(67052805, level);
				} else if (!strcasecmp(type, "pslstreaming")) {
					PIMC_SetAnalyzeCode(55008726, level);
				} else {
					PIMC_SetAnalyzeCode(62780055, level);
				}
				return 0;
			}
			cur = term_argpop(term);
		}
		cur = term_argpop(term);
	}
	return -1;
}
static void player_vpfilter_add(void *mp, int sid, const char *plugin_name, const char *setting) {
	int64_t arg[3] = {0}, res = {0};
	arg[0] = (int64_t)sid; /* sid */
	arg[1] = (int64_t)plugin_name;
	arg[2] = (int64_t)setting;
	PIMediaPlayer_set_player_op(mp, NULL, PIMEDIAPLAYER_OP_ID_ADD_VPFILTER, 3, (void*)arg, 1, &res);
	printf("add vpfilter ret %" PRId64 "\n", res);
}
static void player_vpfilter_remove(void *mp, int sid) {
	int64_t arg[1] = {0}, res = {0};
	arg[0] = (int64_t)sid; /* sid */
	PIMediaPlayer_set_player_op(mp, NULL, PIMEDIAPLAYER_OP_ID_ADD_VPFILTER, 1, (void*)arg, 1, &res);
	printf("remove vpfilter ret %" PRId64 "\n", res);
}
static int player_plugin_dispatch(TERMINAL *term) {
	void *mp = NULL;
	int par_ret = 0;
	const char *sid = NULL, *name = NULL;
	TERM_ARGS *cur = NULL;

	mp = ((USERDATA *)(term->userdata))->player_state->mp;
	if (keyword(term, "custom", "custom", NULL) & MATCH_ACT_FORWARD) {
	}
	if (keyword(term, "spfilter", "spfilter", NULL) & MATCH_ACT_FORWARD) {
	}
	if (keyword(term, "vpfilter", "vpfilter", NULL) & MATCH_ACT_FORWARD) {
		cur = term_argpush(term);
		if (keyword(term, "add", "add", NULL) & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if (argument(term, "sid", "1~65535") & MATCH_ACT_FORWARD) {
				sid = cur->content;
				cur = term_argpush(term);
				par_ret = argument(term, "name", "Plugin name or library file name");
				if (par_ret & MATCH_ACT_EXEC) {
					name = cur->content;
					player_vpfilter_add(mp, atoi(sid), name, NULL);
					return 0;
				}
				if (par_ret & MATCH_ACT_FORWARD) {
					name = cur->content;
					cur = term_argpush(term);
					if (argument(term, "setting", "? Setting for plugin") & MATCH_ACT_EXEC) {
						player_vpfilter_add(mp, atoi(sid), name, cur->content);
						return 0;
					}
					cur = term_argpop(term);
				}
				cur = term_argpop(term);
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "remove", "remove", NULL) & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if (argument(term, "sid", "1~65535") & MATCH_ACT_EXEC) {
				player_vpfilter_remove(mp, atoi(cur->content));
				return 0;
			}
			cur = term_argpop(term);
		}
		cur = term_argpop(term);
	}
	return -1;
}
int console_event_dispatch(TERMINAL *term) {
	int i = 0, ret = 0;
	TERM_ARGS *cur = NULL;
	void *mp = NULL;
	char argv1[32] = {0}, argv2[32] = {0}, *fmt = NULL;
	WINDOW_STATE *window = NULL;
	PLAYER_STATE *player = NULL;
	PI_LISTE *entry = NULL;
	int par_ret = 0;

	cur = term_argpush(term);
	if (((USERDATA *)(term->userdata))->mode == MODE_PLAYER) {
		/* MODE_PLAYER */
		if (keyword(term, "stop", "stop", "Stop Player") & MATCH_ACT_EXEC) {
			mp = ((USERDATA *)(term->userdata))->player_state->mp;
			PIMediaPlayer_stop(mp);
			PIMediaPlayer_shutdown(mp, NULL);
			PIMediaPlayer_dec_ref(mp);
			printf("player (0x%p) stoped\n", mp);
			exit_player_mode(term);
			for (entry = g_players.head; entry != NULL; entry = entry->next) {
				player = (PLAYER_STATE *)entry->payload;
				if (player->mp == mp) {
					pi_list_remove(&g_players, entry);
					return 0;
				}
			}
		}
		if (keyword(term, "start", "start", "Start Player") & MATCH_ACT_EXEC) {
			mp = ((USERDATA *)(term->userdata))->player_state->mp;
			PIMediaPlayer_prepare_async(mp);
			PIMediaPlayer_start(mp);
			printf("player (0x%p) started\n", mp);
			return 0;
		}
		if (keyword(term, "videocallback", "videocallback", "Enable videocallback") & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			if (((par_ret = keyword(term, "i420", "i420", NULL)) & (MATCH_ACT_FORWARD | MATCH_ACT_EXEC)) \
					|| ((par_ret = keyword(term, "nv12", "nv12", NULL)) & (MATCH_ACT_FORWARD | MATCH_ACT_EXEC)) \
					|| ((par_ret = keyword(term, "argb", "argb", NULL)) & (MATCH_ACT_FORWARD | MATCH_ACT_EXEC)) \
					|| ((par_ret = keyword(term, "rgb24", "rgb24", NULL)) & (MATCH_ACT_FORWARD | MATCH_ACT_EXEC))) {
				fmt = cur->content;
				if (par_ret & MATCH_ACT_EXEC) {
					set_player_videocallback(term->userdata, fmt, 1/* savedata */, 0 /* with_ei */);
					return 0;
				}
				if (par_ret & MATCH_ACT_FORWARD) {
					cur = term_argpush(term);
					if (keyword(term, "withei", "withei", "? With extrainfo") & MATCH_ACT_EXEC) {
						set_player_videocallback(term->userdata, fmt, 1/* savedata */, 1 /* with_ei */);
						return 0;
					}
					cur = term_argpop(term);
				}
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "plugin", "plugin", "Enable/Disable Plugin") & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			ret = player_plugin_dispatch(term);
			if (ret == 0) {
				return 0;
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "exit", "exit", NULL) & MATCH_ACT_EXEC) {
			exit_player_mode(term);
			return 0;
		}
		/* MODE_PLAYER */
	} else {
		/* MODE_NORMAL */
#if defined(_WIN32)
		if (keyword(term, "window", "window", "Window operation") & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			ret = window_dispatch(term);
			if (ret == 0) {
				return 0;
			}
			cur = term_argpop(term);
		}
#endif
		if (keyword(term, "player", "player", "Player operation") & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			ret = player_dispatch(term);
			if (ret == 0) {
				return 0;
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "log", "log", "Set Log parameters") & MATCH_ACT_FORWARD) {
			cur = term_argpush(term);
			ret = log_dispatch(term, cur);
			if (ret == 0) {
				return 0;
			}
			cur = term_argpop(term);
		}
		if (keyword(term, "exit", "exit", NULL) & MATCH_ACT_EXEC) {
			term_exit(term);
			return 0;
		}
		/* MODE_NORMAL */
	}
	if (keyword(term, "quit", "quit", NULL) & MATCH_ACT_EXEC) {
		term_exit(term);
		return 0;
	}
	cur = term_argpop(term);
	return -1;
}

#if defined(_WIN32)
static unsigned WINAPI window_thread(void *mArgclist) {
	int need_exit = 0;
	WNDCLASS Draw;
	MSG msg;
	WINDOW_STATE *window = NULL;
	hInstance = GetModuleHandle(NULL);
	Draw.cbClsExtra = 0;
	Draw.cbWndExtra = 0;
	Draw.hCursor = LoadCursor(hInstance, IDC_ARROW);;
	Draw.hIcon = LoadIcon(hInstance, IDI_APPLICATION);;
	Draw.lpszMenuName = NULL;
	Draw.style = CS_HREDRAW | CS_VREDRAW;
	Draw.hbrBackground = (HBRUSH)COLOR_WINDOW;
	Draw.lpfnWndProc = WindowProc;
	Draw.lpszClassName = "ClassName1";
	Draw.hInstance = hInstance;
	RegisterClass(&Draw);

	while (GetMessage(&msg, NULL, 0, 0)) { /* PeekMesssge ? */
		switch (msg.message) {
			case MSG_END_PROCESS: need_exit = 1; break;
			case MSG_NEW_WINDOW:
				window = (WINDOW_STATE *)msg.wParam;
				window->hwnd = create_window(window->name, window->width, window->height);
				// printf("window 0x%p created.\n", window->hwnd);
				pi_list_push(&g_windows, window);
				break;
			case MSG_MOVE_WINDOW:
				window = (WINDOW_STATE *)msg.wParam;
				MoveWindow(window->hwnd, 0, 0, window->width, window->height, 1);
				UpdateWindow(window->hwnd);
				break;
			default:
				TranslateMessage(&msg);
				DispatchMessage(&msg);
		}
		if (need_exit) {
			break;
		}
	}
	return 0;
}
#endif

int main(int argc, char **argv) {
	TERMINAL term;
	USERDATA userdata = {0};
	userdata.mode = MODE_NORMAL;

#if defined(_WIN32)
	unsigned int win_thread_id;
	HANDLE h_win_thread;
	h_win_thread = _beginthreadex(NULL, 0, window_thread, NULL, 0 /* or CREATE_SUSPENDED */, &win_thread_id);
	/* ResumeThread(h_win_thread); call resume if CREATE_SUSPENDED */
	userdata.win_thread_id = win_thread_id;
#endif
	pi_list_init(&g_players, 0, NULL);
	pi_list_init(&g_windows, 0, NULL);
	PIMediaPlayer_global_init("./logs");
	// printf("PIMediaPlayer_global_init(\"./log\")\n"); 
	term_init(&term, "PIMediaPlayer$", console_event_dispatch);
	term_prompt_userdata_set(&term, &userdata);
	term_prompt_color_set(&term, TERM_FGCOLOR_BRIGHT_GREEN | TERM_STYLE_BOLD);
	while (term_readline(&term) == 0);
	term_free(&term);
#if defined(_WIN32)
	PostThreadMessage(win_thread_id, MSG_END_PROCESS, NULL, NULL);
	WaitForSingleObject(h_win_thread, INFINITE);
	CloseHandle(h_win_thread);
#endif
	return 0;
}



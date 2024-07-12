#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include "w32_pthread.h"
#define usleep(a) Sleep((a) / 1000)
#define sleep(a) Sleep((a) * 1000)
#define pthread_detach(a)
#define getpid() GetCurrentThreadId()
#else
#include <unistd.h>
#include <pthread.h>
#endif
#include "terminal.h"

static void *thread_func(void *userdata) {
	int i = 0;
	Terminal *term = (Terminal *)userdata;
	for (i = 0; i < 20; i++) {
		sleep(1);
		term_printf(term, "thread:%d print %d\n", getpid(), i);
	}
	return NULL;
}
static void cmd_print(Terminal *term, int argc, const char **argv) {
	printf("%s\n", argv[1]);
}
static void cmd_sleep(Terminal *term, int argc, const char **argv) {
	printf("sleeping \"%s\" sec\n", argv[2]);
	sleep(atoi(argv[2]));
}
static void cmd_sleepms(Terminal *term, int argc, const char **argv) {
	printf("sleeping \"%s\" ms\n", argv[2]);
	usleep(atoi(argv[2]) * 1000);
}
static void cmd_setprompt(Terminal *term, int argc, const char **argv) {
	int i = 0;
	const char *usr = NULL, *pwd = NULL;
	char *usr_dup = NULL;
	usr = term_getline(term, "user:");
	if (usr == NULL) { /* cancel by Ctrl+C */
		goto func_end;
	}
	usr_dup = strdup(usr); /* usr will be freed in next 'term_getline' or 'term_password' */
	for (i = 0; i < 3; i++) {
		pwd = term_password(term, "password:");
		if (pwd == NULL) {
			goto func_end; /* cancel by Ctrl+C */
		}
		if (0 == strcmp(pwd, "123")) {
			term_prompt_set(term, argv[2]);
			break;
		} else {
			printf("invalid password \"%s\" for user \"%s\", mismatch with \"123\"\n", pwd, usr_dup);
		}
	}
func_end:
	if (usr_dup != NULL) {
		free(usr_dup);
	}
}
static void cmd_printasync(Terminal *term, int argc, const char **argv) {
	static pthread_t task_id;
	pthread_create(&task_id, NULL, thread_func, term);
	pthread_detach(task_id);
}
static void cmd_test0(Terminal *term, int argc, const char **argv) {
	int i = 0;
	for (i = 0; i < argc; i++) {
		printf("%s argv[%d]: %s\n", __func__, i, argv[i]);
	}
}
static void cmd_test1(Terminal *term, int argc, const char **argv) {
	int i = 0;
	for (i = 0; i < argc; i++) {
		printf("%s argv[%d]: %s\n", __func__, i, argv[i]);
	}
}
static void cmd_exit(Terminal *term, int argc, const char **argv) {
	printf("userdata \"%s\"\n", (char *)term_userdata_get(term));
	term_exit(term);
}
int main() {
	Terminal *term;
	TermNode *root = NULL;
	root = term_root_create();

	TermNode *sleepnode = NULL, *unitmsnode  = NULL, *unitsecnode = NULL;
	sleepnode = term_node_child_add(root, TYPE_KEY, "sleep"/*keyword*/, "Sleep Command"/*help*/, NULL/*exec for leaf*/);
	/**/unitsecnode = term_node_child_add(sleepnode, TYPE_KEY, "sec", "Unit: sec", NULL);
	/**//**/term_node_child_add(unitsecnode, TYPE_TEXT, "duration", "Duration with unit sec", cmd_sleep);
	/**/unitmsnode = term_node_child_add(sleepnode, TYPE_KEY, "ms", "Unit: ms", NULL);
	/**//**/term_node_child_add(unitmsnode, TYPE_TEXT, "duration", "Duration with unit ms", cmd_sleepms);

	TermNode *printnode = NULL;
	printnode = term_node_child_add(root, TYPE_KEY, "print", "Print Command", NULL);
	/**/term_node_child_add(printnode, TYPE_TEXT, "content", "Content to be printed", cmd_print);

	term_node_child_add(root, TYPE_KEY, "printasync", "Print in endline mode", cmd_printasync);

	TermNode *testselnode = NULL, *selnode = NULL;
	testselnode = term_node_child_add(root, TYPE_KEY, "testsel", "help for testsel", NULL);
	selnode = term_node_select_add(testselnode, "sel1", NULL);
	/**/term_node_option_add(selnode, "option1", "help for option1");
	/**/term_node_option_add(selnode, "option2", "help for option2");
	/**//**/term_node_child_add(selnode, TYPE_TEXT, "content", "text for all sel1", cmd_test0);

	TermNode *testcolnode = NULL, *colnode = NULL;
	testcolnode = term_node_child_add(root, TYPE_KEY, "testcol", "help for testcol", NULL);
	colnode = term_node_mulsel_add(testcolnode, "col1", MULSEL_OPTIONAL, cmd_test0);
	/**/term_node_option_add(colnode, "optionA", "help for optionA");
	/**/term_node_option_add(colnode, "optionB", "help for optionB");
	/**/term_node_option_add(colnode, "optionC", "help for optionC");
	/**//**/term_node_child_add(colnode, TYPE_KEY, "abc", "text for all col1", cmd_test1);

	TermNode *setnode = NULL, *promptnode = NULL;
	setnode = term_node_child_add(root, TYPE_KEY, "set", "set Command", NULL);
	/**/promptnode = term_node_child_add(setnode, TYPE_KEY, "prompt", "Change prompt", NULL);
	/**//**/term_node_child_add(promptnode, TYPE_TEXT, "content", "Prompt content", cmd_setprompt);

	term_node_child_add(root, TYPE_KEY, "exit", "Exit", cmd_exit);

	term_create(&term, "Demo$", root, "print aa\\ bb\\'\\ncc\\\"\\\\\n"/*init_content*/);
	// term_init(&term, "Demo$", root, NULL);
	term_userdata_set(term, "hello");
	term_loop(term); /* will block */
	term_root_free(root);
	term_destroy(term);
	return 0;
}

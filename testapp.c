#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "terminal.h"

static void cmd_print(Terminal *term, int argc, const char **argv) {
	printf("%s\n", argv[1]);
}
static void cmd_sleep(Terminal *term, int argc, const char **argv) {
	printf("sleeping %s sec\n", argv[2]);
	sleep(atoi(argv[2]));
}
static void cmd_sleepms(Terminal *term, int argc, const char **argv) {
	printf("sleeping %s ms\n", argv[2]);
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
static void cmd_exit(Terminal *term, int argc, const char **argv) {
	printf("userdata \"%s\"\n", (char *)term_userdata_get(term));
	term_exit(term);
}
int main() {
	Terminal term;
	TermNode *root = NULL;
	root = term_root_create();

	TermNode *sleepnode = NULL, *unitmsnode  = NULL, *unitsecnode = NULL;
	sleepnode = term_node_keyword_add(root, "sleep"/*keyword*/, "Sleep Command"/*help*/, NULL/*not NULL if optional*/, NULL/*exec for leaf*/);
	/**/unitsecnode = term_node_keyword_add(sleepnode, "sec", "Unit: sec", "sec", NULL);
	/******/term_node_argument_add(unitsecnode, "duration", "Duration with unit sec", NULL, cmd_sleep);
	/**/unitmsnode = term_node_keyword_add(sleepnode, "ms", "Unit: ms", NULL, NULL);
	/******/term_node_argument_add(unitmsnode, "duration", "Duration with unit ms", NULL, cmd_sleepms);

	TermNode *printnode = NULL;
	printnode = term_node_keyword_add(root, "print", "Print Command", NULL, NULL);
	/**/term_node_argument_add(printnode, "content", "Content to be printed", NULL, cmd_print);

	TermNode *setnode = NULL, *promptnode = NULL;
	setnode = term_node_keyword_add(root, "set", "set Command", NULL, NULL);
	/**/promptnode = term_node_keyword_add(setnode, "prompt", "Change prompt", NULL, NULL);
	/******/term_node_argument_add(promptnode, "content", "Prompt content", NULL, cmd_setprompt);

	term_node_keyword_add(root, "exit", "Exit", NULL, cmd_exit);

	term_init(&term, "Tester$", root, "print aa\\ bb\\'\\ncc\\\"\\\\\n"/*init_content*/);
	term_userdata_set(&term, "hello");
	term_loop(&term); /* will block */
	term_root_free(root);
	term_free(&term);
	return 0;
}

#include <stdio.h>
#include <stdlib.h>
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
static void cmd_exit(Terminal *term, int argc, const char **argv) {
	term_exit(term);
}
int main() {
	Terminal term;
	TermNode *root = NULL;
	root = term_root_create();

	TermNode *sleepnode = NULL, *unitmsnode  = NULL, *unitsecnode = NULL;
	sleepnode = term_node_keyword_add(root, "sleep", "Sleep Command", NULL, NULL);
	/**/unitsecnode = term_node_keyword_add(sleepnode, "sec", "Unit: sec", "sec", NULL);
	/******/term_node_argument_add(unitsecnode, "duration", "Duration with unit sec", NULL, cmd_sleep);
	/**/unitmsnode = term_node_keyword_add(sleepnode, "ms", "Unit: ms", NULL, NULL);
	/******/term_node_argument_add(unitmsnode, "duration", "Duration with unit ms", NULL, cmd_sleepms);

	TermNode *printnode = NULL;
	printnode = term_node_keyword_add(root, "print", "Print Command", NULL, NULL);
	/******/term_node_argument_add(printnode, "content", "Content to be printed", NULL, cmd_print);

	term_node_keyword_add(root, "exit", "Exit", NULL, cmd_exit);
	// term_init(&term, "Tester$", root, NULL);
	term_init(&term, "Tester$", root, "# sleepms 88\n");
	term_loop(&term);
	term_root_free(root);
	term_free(&term);
	return 0;
}

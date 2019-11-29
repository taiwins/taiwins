#ifndef _GNU_SOURCE /* for qsort_r */
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <rax.h>
#include <desktop_entry.h>
#include <os/exec.h>
#include <strops.h>
#include "console_module.h"
#include <vector.h>


static inline void
tolowers(char *line)
{
	for (; *line; line++) *line = tolower(*line);
}

void remove_exec_param(char *exec)
{
	char *ptr = exec;
	while (*ptr) {
		if (*ptr == '%' && !isspace(*(ptr+1)))
			*ptr = '\0';
		ptr++;
	}
}

static int
xdg_app_module_exec(struct console_module *module, const char *entry,
		    char **result)
{
	struct xdg_app_entry *app = NULL;
	char *name = strop_ltrim((char *)entry);
	char execpy[256];
	char *argv[128] = {0};

	vector_for_each(app, (vector_t *)module->user_data) {
		if (strcasecmp(name, app->name) == 0)
			break;
	}
	if (!app)
		return -1;
	strcpy(execpy, app->exec);
	remove_exec_param(execpy);

	int argc = 0;
	char *tok, *saved_ptr = NULL;
	for (tok = strtok_r(execpy, " ", &saved_ptr); tok;
	     tok = strtok_r(NULL, " ", &saved_ptr)) {
		argv[argc] = tok;
		argc += 1;
	}
	fork_exec(argc, argv);
	return 0;
}

static int
xdg_app_module_search(struct console_module *module, const char *to_search,
		      vector_t *result)
{
	struct xdg_app_entry *app = NULL;
	//the search need to done for (strstr(x, to_search) for all x in my vec.)
	//there should be a effective algorithm for that.

	vector_init_zero(result, sizeof(console_search_entry_t),
			 free_console_search_entry);
	//TODO replace this brute force with a more efficient algorithm
	vector_for_each(app, (vector_t *)module->user_data) {
		if (strcasestr(app->name, to_search)) {
			console_search_entry_t *entry =
				vector_newelem(result);
			if (strlen(app->name) < 32) {
				strcpy(entry->sstr, app->name);
				entry->pstr = NULL;
			} else
				entry->pstr = strdup(app->name);
		}
	}
	//now we can have a sort, but it requires edit distance
	/* qsort_r(result->elems, result->len, result->elemsize, NULL, to_search); */
	return 0;
}

static bool
xdg_app_filter(const char *command, const char *last)
{
	return strcasestr(last, command) != NULL;
}


static void
xdg_app_module_init(struct console_module *module)
{
	//use signal for this
	//TODO check signal to terminate children if children terminated
	vector_t *apps = module->user_data;

	*apps = xdg_apps_gather();
	//insert into spaces
	struct xdg_app_entry *app = NULL;
	vector_for_each(app, apps) {
		char *name = strdup(app->name);
		tolowers(name);
		free(name);
	}
}


static void
xdg_app_module_destroy(struct console_module *module)
{
	vector_t *apps = module->user_data;
	vector_destroy(apps);
}


static vector_t xdg_app_vector;

struct console_module app_module = {
	.name = "MODULE_APP",
	.exec = xdg_app_module_exec,
	.search = xdg_app_module_search,
	.init_hook = xdg_app_module_init,
	.destroy_hook = xdg_app_module_destroy,
	.filter_test = xdg_app_filter,
	.support_cache = true,
	.user_data = &xdg_app_vector,
};

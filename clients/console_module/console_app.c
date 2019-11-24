#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <rax.h>
#include <desktop_entry.h>
#include "console_module.h"
#include "vector.h"

static inline void
tolowers(char *line)
{
	for (; *line; line++) *line = tolower(*line);
}


static int
xdg_app_module_exec(struct console_module *module, const char *entry,
		    char **result)
{
	return 0;
}


static int
xdg_app_module_search(struct console_module *module, const char *to_search,
		      vector_t *result)
{
	struct raxIterator iter;

	if (!module->radix)
		return 0;
	//the search need to done for (strstr(x, to_search) for all x in my vec.)
	//there should be a effective algorithm for that.

	vector_init_zero(result, sizeof(console_search_entry_t),
			 free_console_search_entry);

	raxStart(&iter, module->radix);

	return 0;
}


static void
xdg_app_module_init(struct console_module *module)
{
	vector_t *apps = module->user_data;

	*apps = xdg_apps_gather();
	module->radix = raxNew();
	//insert into spaces
	struct xdg_app_entry *app = NULL;
	vector_for_each(app, apps) {
		char *name = strdup(app->name);
		tolowers(name);
		raxInsert(module->radix, (unsigned char *)name, strlen(name),
			  app, NULL);
		free(name);
	}
}


static void
xdg_app_module_destroy(struct console_module *module)
{
	raxFree(module->radix);
	vector_destroy(module->user_data);
}


static vector_t xdg_app_vector;

struct console_module app_module = {
	.name = "MODULE_APP",
	.exec = xdg_app_module_exec,
	.search = xdg_app_module_search,
	.init_hook = xdg_app_module_init,
	.destroy_hook = xdg_app_module_destroy,
	.support_cache = true,
	.user_data = &xdg_app_vector,
};

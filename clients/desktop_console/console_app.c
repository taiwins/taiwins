/*
 * console_app.c - taiwins client console app module
 *
 * Copyright (c) 2019-2020 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

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

#include <twclient/desktop_entry.h>
#include <twclient/image_cache.h>
#include <ctypes/os/exec.h>
#include <ctypes/strops.h>
#include <ctypes/vector.h>
#include <ctypes/hash.h>

#include "console.h"

static struct app_module_data {
	vector_t  xdg_app_vector;
	vector_t icons;
} module_data = {0};

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
	struct app_module_data *userdata = module->user_data;
	char *name = strop_ltrim((char *)entry);
	char execpy[256];
	char *argv[128] = {0};

	vector_for_each(app, &userdata->xdg_app_vector) {
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
	struct nk_image *icon = NULL;
	console_search_entry_t *entry = NULL;
	struct app_module_data *userdata = module->user_data;
	vector_t *apps = &userdata->xdg_app_vector;
	vector_t *icons = &userdata->icons;

	vector_init_zero(result, sizeof(console_search_entry_t),
			 search_entry_free);

	//TODO replace this brute force with a more efficient algorithm
	for (int i = 0; i < apps->len; i++) {
		app = vector_at(apps, i);
		icon = vector_at(icons, i);
		if (strcasestr(app->name, to_search)) {
			 entry = vector_newelem(result);
			if (strlen(app->name) < 32) {
				strcpy(entry->sstr, app->name);
				entry->pstr = NULL;
			} else
				entry->pstr = strdup(app->name);
			entry->img = *icon;
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

/*******************************************************************************
 * inits
 ******************************************************************************/

/**
 * @brief update the desktop_entry icons with cache
 *
 * create a hash search table first, then go through all the xdg_app_entrys, if
 * it does not hav a icon, try to update with this cache.
 */
static void
xdg_app_module_update_icons(struct console_module *module)
{
	struct app_module_data *userdata = module->user_data;
	struct xdg_app_entry *app = NULL;
	vector_t *apps = &userdata->xdg_app_vector;
	vector_t *images = &userdata->icons;
	struct nk_image empty;
	const struct nk_image *requested;
	char *key;

	//retrieving images from hash table
	empty = nk_image_id(0);
	for (int i = 0; i < apps->len; i++) {
		app = vector_at(apps, i);
		key = app->icon;
		requested = desktop_console_request_image(
			module->console, key, "application-x-executable");
		if (requested)
			memcpy(vector_at(images, i), requested,
			       sizeof(struct nk_image));
		else
			memcpy(vector_at(images, i), &empty,
			       sizeof(struct nk_image));
	}
}

static void
xdg_app_module_init(struct console_module *module)
{
	struct app_module_data *userdata = module->user_data;
	struct wl_array apps_data;
	vector_t *apps = &userdata->xdg_app_vector;
	vector_t *images = &userdata->icons;

	//gather desktop entries
	apps_data = xdg_apps_gather();
	vector_init_zero(apps, sizeof(struct xdg_app_entry), NULL);
	apps->elems = apps_data.data;
	apps->len = apps_data.size / apps->elemsize;
	apps->alloc_len = apps_data.alloc / apps->elemsize;

	vector_init_zero(images, sizeof(struct nk_image), NULL);
	vector_resize(images, apps->len);
	memset(images->elems, 0, sizeof(struct nk_image) * apps->len);

	//I can actually directly do it here
	xdg_app_module_update_icons(module);
}

static void
xdg_app_module_destroy(struct console_module *module)
{
	struct app_module_data *userdata = module->user_data;

	vector_destroy(&userdata->xdg_app_vector);
	vector_destroy(&userdata->icons);
}


struct console_module app_module = {
	.name = "MODULE_APP",
	.exec = xdg_app_module_exec,
	.search = xdg_app_module_search,
	.init_hook = xdg_app_module_init,
	.destroy_hook = xdg_app_module_destroy,
	.filter_test = xdg_app_filter,
	.support_cache = true,
	.supported_icons = CONSOLE_ICON_APP | CONSOLE_ICON_MIME,
	.user_data = &module_data,
};

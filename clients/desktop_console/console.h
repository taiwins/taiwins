/*
 * console.h - taiwins client console header
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

#ifndef TW_CONSOLE_H
#define TW_CONSOLE_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <wayland-client.h>

#include <ctypes/os/file.h>
#include <ctypes/os/exec.h>
#include <twclient/client.h>
#include <twclient/ui.h>
#include <rax.h>
#include <twclient/nk_backends.h>

#include <shared_config.h>

#ifdef __cplusplus
extern "C" {
#endif


struct desktop_console;
struct console_module;
/** public console data shared by console modules **/

/* this is majorly for adding images, an alternative is for console_module to
 * request images, does it need application images? mime-images? categorie
 * images? places images? and console could return a generic image if not
 * found */
struct nk_wl_backend *
desktop_console_aquire_nk_backend(struct desktop_console *console);

const struct nk_image*
desktop_console_request_image(struct desktop_console *console,
                              const char *name, const char *fallback);
/* load self defined icons */
void
desktop_console_load_icons(struct desktop_console *console,
                            const struct wl_array *handle_list,
                            const struct wl_array *string_list);
void
desktop_console_append_module(struct desktop_console *console,
                              struct console_module *module);
void *
desktop_console_run_config_lua(struct desktop_console *console,
                               const char *path);

void
desktop_console_release_lua_config(struct desktop_console *console,
                                   void *config_data);

/* critical race condition code is wrapped here, so it would be transparent
 * to console itself */
int
desktop_console_take_search_result(struct console_module *module,
                                  vector_t *ret);

int
desktop_console_take_exec_result(struct console_module *module,
                                char **result);


enum console_icon_type {
	CONSOLE_ICON_APP = 1 << 0,
	CONSOLE_ICON_MIME = 1 << 1,
	CONSOLE_ICON_PLACE = 1 << 2,
	CONSOLE_ICON_STATUS = 1 << 3,
	CONSOLE_ICON_DEVICE = 1 << 4,
};

/**
 * @brief a console module provides its set of features to the console
 *
 * example application are like: exec a command; launch an application; submit a
 * setting; find file; apply themes; install plugins.
 *
 */
struct console_module {
	const char name[32];
	struct desktop_console *console;
	struct rax *radix;
	uint32_t supported_icons;
	const bool support_cache;
	bool quit;


	struct {
		pthread_mutex_t command_mutex;
		char *search_command, *exec_command;
	};
	struct {
		pthread_mutex_t results_mutex;
		vector_t search_results;
		int search_ret, exec_ret;
		char *exec_res;
	};

	int (*search)(struct console_module *, const char *, vector_t *);
	int (*exec)(struct console_module *, const char *, char **);
	bool (*filter_test)(const char *, const char *);

	void (*init_hook)(struct console_module *);
	void (*destroy_hook)(struct console_module *);

	pthread_t thread;
	sem_t semaphore;
	void *user_data;
};

void
console_module_init(struct console_module *module,
                    struct desktop_console *console);
void
console_module_release(struct console_module *module);

void
console_module_command(struct console_module *module, const char *search,
                       const char *exec);


//all the search component returns this
typedef struct {
	struct nk_image img;
	char sstr[32]; /**< small string optimization */
	char *pstr;
} console_search_entry_t;

static inline bool
search_entry_empty(const console_search_entry_t *entry)
{
	return strlen(entry->sstr) == 0 &&
		entry->pstr == NULL;
}

static inline const char *
search_entry_get_string(const console_search_entry_t *entry)
{
	return (entry->pstr) ? entry->pstr : &entry->sstr[0];
}

static inline void
search_entry_move(console_search_entry_t *dst, console_search_entry_t *src)
{
	dst->img = src->img;
	dst->pstr = src->pstr;
	memcpy(dst->sstr, src->sstr, sizeof(src->sstr));
	src->pstr = NULL;
}

extern bool
search_entry_equal(console_search_entry_t *, console_search_entry_t *);

extern void
search_entry_assign(void *dst, const void *src);

extern void
search_entry_free(void *);


extern struct console_module cmd_module;
extern struct console_module path_module;
extern struct console_module app_module;


#ifdef __cplusplus
}
#endif


#endif /* EOF */

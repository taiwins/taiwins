/*
 * console_module.h - taiwins client console module header
 *
 * Copyright (c) 2019 Xichen Zhou
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

#ifndef TW_CONSOLE_MODULE_H
#define TW_CONSOLE_MODULE_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include <wayland-client.h>
#include <os/file.h>
#include <os/exec.h>

#include <client.h>
#include <ui.h>
#include <rax.h>
#include <nk_backends.h>

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief a console module provides its set of features to the console
 *
 * example application are like: exec a command; launch an application; submit a
 * setting; find file; apply themes; install plugins.
 *
 */
struct console_module {
	struct desktop_console *console;
	struct nk_wl_backend *bkend;
	struct rax *radix;
	const bool support_cache;
	const char name[32];
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
	/**< thread init, called after  **/
	void (*thread_init)(struct console_module *);
	void (*destroy_hook)(struct console_module *);

	pthread_t thread;
	sem_t semaphore;
	void *user_data;
};

void console_module_init(struct console_module *module,
                         struct desktop_console *console,
                         struct nk_wl_backend *bkend);

void console_module_release(struct console_module *module);

void console_module_command(struct console_module *module, const char *search,
			    const char *exec);

/* critical race condition code is wrapped here, so it would be transparent
 * to console itself */
int console_module_take_search_result(struct console_module *module,
				      vector_t *ret);

int console_module_take_exec_result(struct console_module *module,
				    char **result);

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

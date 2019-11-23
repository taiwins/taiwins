#ifndef TW_CONSOLE_H
#define TW_CONSOLE_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include <wayland-client.h>
#include <wayland-taiwins-desktop-client-protocol.h>
#include <os/exec.h>

#include <client.h>
#include <ui.h>
#include <rax.h>
#include <nk_backends.h>
#include "../shared_config.h"

#include "common.h"
#include "vector.h"

#ifdef __cplusplus
extern "C" {
#endif

struct desktop_console {
	struct tw_console *interface;
	struct tw_ui *proxy;
	struct wl_globals globals;
	struct app_surface surface;
	struct shm_pool pool;
	struct wl_buffer *decision_buffer;
	struct nk_wl_backend *bkend;
	struct wl_callback *exec_cb;
	uint32_t exec_id;

	off_t cursor;
	char chars[256];
	bool quit;
	//a good hack is that this text_edit is stateless, we don't need to
	//store anything once submitte
	struct nk_text_edit text_edit;

	vector_t modules;
	vector_t search_results; //search results from modules moves to here
	uint32_t select_navigation[2]; //{module_idx, element_idx}
};

/**
 * @brief a console module provides its set of features to the console
 *
 * example application are like: exec a command; launch an application; submit a
 * setting; find file; apply themes; install plugins.
 *
 */
struct console_module {
	struct desktop_console *console;
	struct rax *radix;
	const bool support_cache;
	const char name[32];

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

void console_module_init(struct console_module *module,
			 struct desktop_console *console);

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
	char sstr[32]; //small string optimization
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

extern void search_entry_assign(void *dst, const void *src);
extern void free_console_search_entry(void *);


extern struct console_module cmd_module;
extern struct console_module path_module;
extern struct console_module app_module;


#ifdef __cplusplus
}
#endif


#endif /* EOF */

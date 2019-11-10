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
	//store anything once submitted
	struct nk_text_edit text_edit;
	vector_t completions;

	vector_t modules;
/*
 * A good example here is ulauncher, it has many different extensions and
 * supports configuration, in that case though, we will rely on lua bindings
 */
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

	void (*init_hook)(struct console_module *);
	void (*destroy_hook)(struct console_module *);

	pthread_t thread;
	sem_t semaphore;
	void *user_data;
};

void console_module_init(struct console_module *module);

void console_module_release(struct console_module *module);

void console_module_command(struct console_module *module, const char *search,
			    const char *exec);

/* critical race condition code is wrapped here, so it would be transparent
 * to console itself */
int console_module_take_search_result(struct console_module *module,
				      vector_t *ret);

int console_module_take_exec_result(struct console_module *module,
				    char **result);


typedef char console_cmd_t[256];
extern struct console_module cmd_module;

typedef struct {
	struct nk_image img;
	char exec[256];
} console_app_t;
extern struct console_module app_module;

typedef char *console_path_t;
extern struct console_module path_module;


#ifdef __cplusplus
}
#endif


#endif /* EOF */

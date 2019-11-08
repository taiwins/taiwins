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

struct desktop_console;

/**
 * @brief a console module provides its set of features to the console
 *
 */
struct console_module {
	//it supposed to have a vector of search result. Mutex
	//rax and cache module
	struct desktop_console *console;
	struct rax *radix;
	char command[256];
	char *exec_command;

	vector_t search_results;
	//features we want:
	//search.
	void (*search)(struct console_module *, const char *);
	//exec a command, launch an application. submit a setting, open a file,
	//apply themes. install plugins
	void (*exec)(struct console_module *, const char *entry);

	pthread_t thread;

	pthread_mutex_t mutex;
	pthread_cond_t condition;
	sem_t sem;
};

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
	rax *rax;
/*
 * A good example here is ulauncher, it has many different extensions and
 * supports configuration, in that case though, we will rely on lua bindings
 */
};

void
console_module_release(struct console_module *module)
{
	raxFree(module->radix);
}


void *thread_run_module(void *arg);

void
console_module_init(struct console_module *module)
{
	if (pthread_create(&module->thread, NULL, thread_run_module,
			   (void *)module))
		return;
}




#ifdef __cplusplus
}
#endif


#endif /* EOF */

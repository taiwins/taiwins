#ifndef TW_CONSOLE_H
#define TW_CONSOLE_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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

struct console_module {
	//it supposed to have a vector of search result. Mutex
	//rax and cache module
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
};



#ifdef __cplusplus
}
#endif


#endif /* EOF */

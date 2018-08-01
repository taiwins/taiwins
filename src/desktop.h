#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include "shell.h"

#ifdef  __cplusplus
extern "C" {
#endif


//wayland-desktop implementation, you can seperate this from  the shell
#define DECISION_STRIDE TAIWINS_LAUNCHER_CONF_STRIDE
#define NUM_DECISIONS TAIWINS_LAUNCHER_CONF_NUM_DECISIONS

/**
 ** right now both the client and server have to keep this struct. We could
 ** somehow create this record in a internal include file to avoid duplicating
 ** this work
 */
struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));

//we have to make either taiwins_shell and launcher a api to the other
struct launcher {
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_resource *launcher;
	struct wl_listener destroy_listener;
	struct wl_listener close_listener;
	struct wl_resource *callback;
	unsigned int n_execs;
};

struct twdesktop;


/** add desktop protocols **/
struct twdesktop *announce_desktop(struct weston_compositor *compositor, struct twshell *shell);
//void add_desktop_bindings(struct weston_compositor *c);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */

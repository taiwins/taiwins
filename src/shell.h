#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <compositor.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>

#ifdef  __cplusplus
extern "C" {
#endif


#define TWSHELL_VERSION 1

/** announcing the taiwins shell global
 *
 * @param compositor weston compositor global
 */
void announce_shell(struct weston_compositor *compositor);
struct weston_layer *twshell_get_ui_layer(void);
struct weston_layer *twshell_get_background_layer(void);

//wayland-desktop implementation, you can seperate this from  the shell

//we have to make either taiwins_shell and launcher a api to the other
struct taiwins_launcher {
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_resource *launcher;
};

struct workspace {
	/* a workspace have three layers,
	 * - the hiden layer that you won't be able to see, because it is covered by
	 shown float but we don't insert the third layer to
	 * the compositors since they are hiden for floating views. The postions
	 * of the two layers change when user interact with windows.
	 */
	struct weston_layer hiden_float_layout;
	struct weston_layer tile_layout;
	struct weston_layer shown_float_layout;
	//the wayland-buffer
};

struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
};

/**
 * get the launcher for the shell
 */
struct taiwins_launcher *twshell_acquire_launcher(void);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

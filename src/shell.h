#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <compositor.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <sequential.h>

#ifdef  __cplusplus
extern "C" {
#endif


#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1

/** announcing the taiwins shell global
 *
 * @param compositor weston compositor global
 */
void announce_shell(struct weston_compositor *compositor);
void add_shell_bindings(struct weston_compositor *ec);

struct weston_layer *get_shell_ui_layer(void);
struct weston_layer *get_shell_background_layer(void);


//wayland-desktop implementation, you can seperate this from  the shell
struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
};

//we have to make either taiwins_shell and launcher a api to the other
struct launcher {
	struct wl_shm_buffer *decision_buffer;
	struct weston_surface *surface;
	struct wl_resource *launcher;
};

struct workspace;

struct desktop {
	/* interface with the client */
	struct launcher launcher;
	/* managing current status */
	struct workspace *actived_workspace[2];
	vector_t workspaces;
	struct weston_compositor *compositor;
};

/** announcing the taiwins launcher global
 *
 * @param
 */
void annouce_desktop(struct weston_compositor *compositor);
void add_desktop_bindings(struct weston_compositor *c);
/**
 * get the launcher for the shell
 */
struct launcher *twshell_acquire_launcher(void);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

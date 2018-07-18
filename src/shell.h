#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <libweston-desktop.h>
#include <compositor.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <sequential.h>

#ifdef  __cplusplus
extern "C" {
#endif


#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1


//we have this hiden struct for APIs
struct twshell;
/** announcing the taiwins shell global
 *
 * @param compositor weston compositor global
 */
void announce_shell(struct weston_compositor *compositor);
void add_shell_bindings(struct weston_compositor *ec);

/* the effect to unify the surface view creation */
bool twshell_set_ui_surface(struct twshell *shell,
			    struct wl_resource *wl_surface,
			    struct wl_resource *output, struct wl_resource *wl_resource,
			    struct wl_listener *listener);

struct weston_layer *get_shell_ui_layer(void);
struct weston_layer *get_shell_background_layer(void);


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
};

struct workspace;

struct desktop {
	/* interface with the client */
	struct launcher launcher;
	/* managing current status */
	struct workspace *actived_workspace[2];
	vector_t workspaces;
	struct weston_compositor *compositor;
	struct weston_desktop *api;

	struct wl_listener destroy_listener;
};

/** announcing the taiwins launcher global
 *
 * @param
 */
bool announce_desktop(struct weston_compositor *compositor);
void add_desktop_bindings(struct weston_compositor *c);
/**
 * get the launcher for the shell
 */
struct launcher *twshell_acquire_launcher(void);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

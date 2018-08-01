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
 *
 * this adds the signal to destroy the shell as well.
 */
struct twshell *announce_twshell(struct weston_compositor *compositor, const char *path);
struct wl_client *twshell_get_client(struct twshell *shell);

void add_shell_bindings(struct weston_compositor *ec);

/* this function is getting too many arguments */
bool twshell_set_ui_surface(struct twshell *shell,
			    struct weston_surface *surface, struct weston_output *output,
			    struct wl_resource *wl_resource,
			    int32_t x, int32_t y);

void twshell_close_ui_surface(struct weston_surface *surface);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

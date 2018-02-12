#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <compositor.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif


#define TWSHELL_VERSION 1

/** announcing the taiwins shell global
 *
 * @param compositor weston compositor global
 */
void announce_shell(struct weston_compositor *compositor);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

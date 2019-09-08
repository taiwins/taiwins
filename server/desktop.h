#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include <wayland-server.h>
#include <sequential.h>
#include <wayland-taiwins-desktop-server-protocol.h>
#include "bindings.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TWSHELL_VERSION 1
#define TWDESKP_VERSION 1

struct shell;
struct desktop;
struct console;
struct taiwins_config;
struct shell_ui;

/**
 * @brief annouce globals
 */
struct shell *announce_shell(struct weston_compositor *compositor, const char *path,
			     struct taiwins_config *config);

struct console *announce_console(struct weston_compositor *compositor,
				 struct shell *shell, const char *exec_path,
				 struct taiwins_config *config);

struct desktop *announce_desktop(struct weston_compositor *compositor,
				 struct shell *shell,
				 struct taiwins_config *config);

void annouce_theme(struct weston_compositor *ec, struct shell *shell,
		   struct taiwins_config *config);

// other APIs

struct wl_client *shell_get_client(struct shell *shell);

void shell_create_ui_elem(struct shell *shell, struct wl_client *client,
			  uint32_t tw_ui, struct wl_resource *wl_surface,
			  uint32_t x, uint32_t y, enum tw_ui_type type);

void shell_post_data(struct shell *shell, uint32_t type, struct wl_array *msg);
void shell_post_message(struct shell *shell, uint32_t type, const char *msg);

struct weston_geometry
shell_output_available_space(struct shell *shell, struct weston_output *weston_output);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */

#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include "shell.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct desktop;
struct console;

struct console *announce_console(struct weston_compositor *compositor,
				       struct shell *shell, const char *exec_path);

struct desktop *announce_desktop(struct weston_compositor *compositor);
void end_desktop(struct desktop *desktop);


void desktop_add_bindings(struct desktop *d, struct tw_binding_node *key_bindings,
			  struct tw_binding_node *btn_bindings,
			  struct tw_binding_node *axis_bindings,
			  struct tw_binding_node *touch_bindings);

void console_add_bindings(struct console *d, struct tw_binding_node *key_bindings,
			  struct tw_binding_node *btn_bindings,
			  struct tw_binding_node *axis_bindings,
			  struct tw_binding_node *touch_bindings);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

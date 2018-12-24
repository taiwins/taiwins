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


/////////////// Desktop bindings ///////////////////////

//because you cannot declare directly the bindings, so you have to declare here
//and bind them in the another file

//// operations on the output
extern weston_axis_binding_handler_t desktop_zoom_binding;

//// operations on workspaces
extern weston_key_binding_handler_t desktop_workspace_switch_binding;
extern weston_key_binding_handler_t desktop_workspace_switch_recent_binding;

//// operations on the views
extern weston_axis_binding_handler_t desktop_alpha_binding;
extern weston_button_binding_handler_t desktop_move_binding;
extern weston_button_binding_handler_t desktop_click_focus_binding;
extern weston_touch_binding_handler_t desktop_touch_focus_binding;
extern weston_touch_binding_handler_t desktop_active_tchbinding;
extern weston_key_binding_handler_t desktop_resize_keybinding;
extern weston_key_binding_handler_t desktop_deplace_binding;
extern weston_key_binding_handler_t desktop_focus_binding;
extern weston_key_binding_handler_t desktop_change_view_ws_binding;

/// now we have to figure out how to have user define the keys, but we should
/// wait the input system is ready



#ifdef  __cplusplus
}
#endif

#endif /* EOF */

#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include "shell.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct tw_desktop;
struct tw_console;

struct tw_console *announce_console(struct weston_compositor *compositor,
				       struct twshell *shell, const char *exec_path);

struct tw_desktop *announce_desktop(struct weston_compositor *compositor);
void end_desktop(struct tw_desktop *desktop);


/////////////// Desktop bindings ///////////////////////

//because you cannot declare directly the bindings, so you have to declare here
//and bind them in the another file

//// operations on the output
extern weston_axis_binding_handler_t tw_desktop_zoom_binding;

//// operations on workspaces
extern weston_key_binding_handler_t tw_desktop_workspace_switch_binding;
extern weston_key_binding_handler_t tw_desktop_workspace_switch_recent_binding;

//// operations on the views
extern weston_axis_binding_handler_t tw_desktop_alpha_binding;
extern weston_button_binding_handler_t tw_desktop_move_binding;
extern weston_button_binding_handler_t tw_desktop_click_focus_binding;
extern weston_touch_binding_handler_t tw_desktop_touch_focus_binding;
extern weston_touch_binding_handler_t tw_desktop_active_tchbinding;
extern weston_key_binding_handler_t tw_desktop_resize_keybinding;
extern weston_key_binding_handler_t tw_desktop_deplace_binding;
extern weston_key_binding_handler_t tw_desktop_focus_binding;
extern weston_key_binding_handler_t tw_desktop_change_view_ws_binding;

/// now we have to figure out how to have user define the keys, but we should
/// wait the input system is ready



#ifdef  __cplusplus
}
#endif

#endif /* EOF */

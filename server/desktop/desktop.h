#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include "../shell.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct twdesktop;
struct twlauncher;

struct twlauncher *announce_twlauncher(struct weston_compositor *compositor,
				       struct twshell *shell, const char *exec_path);

/** TODO add desktop protocols **/
struct twdesktop *announce_desktop(struct weston_compositor *compositor, struct twlauncher *launcher);
void end_twdesktop(struct twdesktop *desktop);


/////////////// Desktop bindings ///////////////////////

//because you cannot declare directly the bindings, so you have to declare here
//and bind them in the another file

//// operations on the output
extern weston_axis_binding_handler_t twdesktop_zoom_binding;

//// operations on workspaces
extern weston_key_binding_handler_t twdesktop_workspace_switch_binding;
extern weston_key_binding_handler_t twdesktop_workspace_switch_recent_binding;

//// operations on the views
extern weston_axis_binding_handler_t twdesktop_alpha_binding;
extern weston_button_binding_handler_t twdesktop_move_binding;
extern weston_button_binding_handler_t twdesktop_click_focus_binding;
extern weston_touch_binding_handler_t twdesktop_touch_focus_binding;
extern weston_touch_binding_handler_t twdesktop_active_tchbinding;
extern weston_key_binding_handler_t twdesktop_resize_keybinding;
extern weston_key_binding_handler_t twdesktop_deplace_binding;
extern weston_key_binding_handler_t twdesktop_focus_binding;
extern weston_key_binding_handler_t twdesktop_change_view_ws_binding;

/// now we have to figure out how to have user define the keys, but we should
/// wait the input system is ready



#ifdef  __cplusplus
}
#endif

#endif /* EOF */

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


/////////////// Desktop functionalities ///////////////////////

//// operations on the output
weston_axis_binding_handler_t twdesktop_zoom_binding;

//// operations on workspaces
weston_key_binding_handler_t twdesktop_switch_ws_binding;
weston_key_binding_handler_t twdesktop_switch_recent_ws_binding;

//// operations on the views
weston_axis_binding_handler_t twdesktop_alpha_binding;
weston_button_binding_handler_t twdesktop_move_binding;
weston_button_binding_handler_t twdesktop_click_focus_binding;
weston_touch_binding_handler_t twdesktop_touch_focus_binding;
weston_touch_binding_handler_t twdesktop_active_tchbinding;
weston_key_binding_handler_t twdesktop_resize_keybinding;
weston_key_binding_handler_t twdesktop_deplace_binding;
weston_key_binding_handler_t twdesktop_focus_binding;
weston_key_binding_handler_t twdesktop_change_view_ws_binding;

/// now we have to figure out how to have user define the keys, but we should
/// wait the input system is ready



#ifdef  __cplusplus
}
#endif

#endif /* EOF */

#include <wayland-util.h>
#include "ui.h"
#include "widget.h"

struct wl_list *
shell_widget_create_with_funcs(nk_egl_draw_func_t draw_cb,
			       nk_egl_postcall_t post_cb,
			       appsurf_draw_t anchor_cb,
			       size_t width, size_t height,
			       size_t scale)
{
	struct shell_widget *widget = malloc(sizeof(struct shell_widget));
	wl_list_init(&widget->link);
}

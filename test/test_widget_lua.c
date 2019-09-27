#include <ui.h>
#include <widget.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <wayland-util.h>


void shell_widget_load_script(struct wl_list *head, const char *path);

int main(int argc, char *argv[])
{
	struct shell_widget *w, *tmp;
	struct wl_list head;
	wl_list_init(&head);
	shell_widget_load_script(&head, argv[1]);
	wl_list_for_each_safe(w, tmp, &head, link) {
		wl_list_remove(&w->link);
		shell_widget_disactive(w);
	}

	return 0;
}

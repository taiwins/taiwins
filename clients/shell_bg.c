#include "shell.h"


static bool
shell_background_start_menu(struct app_surface *surf, const struct app_event *e)
{
	if (e->ptr.btn == BTN_RIGHT && e->ptr.state)
		fprintf(stderr, "should start menu by now");
	return true;
}

static void
shell_background_frame(struct app_surface *surf, struct wl_buffer *buffer,
		       struct bbox *geo)

{
	//now it respond only app_surface_frame, we only need to add idle task
	//as for app_surface_frame later
	struct shell_output *output = container_of(surf, struct shell_output, background);
	struct desktop_shell *shell = output->shell;
	*geo = surf->allocation;
	void *buffer_data = shm_pool_buffer_access(buffer);
	if (!strlen(shell->wallpaper_path))
		sprintf(shell->wallpaper_path,
			"%s/.wallpaper/wallpaper.png", getenv("HOME"));
	if (!nk_wl_load_image_for_buffer(shell->wallpaper_path, surf->pool->format,
		       surf->allocation.w*surf->allocation.s,
		       surf->allocation.h*surf->allocation.s,
		       (unsigned char *)buffer_data)) {
		fprintf(stderr, "failed to load image somehow\n");
	}
}

static void
shell_background_impl_filter(struct wl_list *head,
			     struct app_event_filter *filter)
{
	wl_list_init(&filter->link);
	filter->type = TW_POINTER_BTN;
	filter->intercept = shell_background_start_menu;
	wl_list_insert(head, &filter->link);
}


void
shell_init_bg_for_output(struct shell_output *w)
{
	struct desktop_shell *shell = w->shell;
	//background
	struct wl_surface *bg_sf =
		wl_compositor_create_surface(shell->globals.compositor);
	w->bg_ui =
		tw_shell_create_background(shell->interface, bg_sf, w->index);

	app_surface_init(&w->background, bg_sf,
			 &shell->globals, APP_SURFACE_BACKGROUND,
			 APP_SURFACE_NORESIZABLE);
	shm_buffer_impl_app_surface(&w->background,
				    shell_background_frame,
				    w->bbox);
	shell_background_impl_filter(&w->background.filter_head,
				     &w->background_events);
}

void
shell_resize_bg_for_output(struct shell_output *w)
{
	//TODO hacks here, we temporarily turn off non resizable flags
	w->background.flags &= ~APP_SURFACE_NORESIZABLE;
	app_surface_resize(&w->background, w->bbox.w, w->bbox.h, w->bbox.s);
	w->background.flags |= APP_SURFACE_NORESIZABLE;
}

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cairo/cairo.h>


#include <wayland-client.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include "client.h"
#include "ui.h"

/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE TAIWINS_LAUNCHER_CONF_STRIDE
#define NUM_DECISIONS TAIWINS_LAUNCHER_CONF_NUM_DECISIONS

//every decision represents a row in wl_buffer, we need to make it as aligned as possible
struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));


struct desktop_launcher {
	struct taiwins_launcher *interface;
	struct wl_globals globals;
	struct app_surface launcher_surface;
	struct wl_buffer *decision_buffer;
	struct shm_pool pool;
	//we don't have a shm pool here
	off_t cursor;
	char chars[256];
	bool quit;
};

/**
 * @brief launcher manipulate interface
 *
 * This function does all the input handling, decide what to do with the
 * launcher, manipulate the launcher chars and eventually launch an application.
 *
 * for this to work, we need an actual exec and a line editing library, this is why this shit is hard.
 */
static void key_handler(struct app_surface *surf, xkb_keysym_t keysym, uint32_t modifier);



static void
ready_the_launcher(struct desktop_launcher *launcher)
{
	struct wl_shm *shm = launcher->globals.shm;
	memset(launcher->chars, 0, sizeof(launcher->chars));
	struct bbox bounding = {
		.x = 0, .y = 0,
		.w = 400, .h = 20
	};

	shm_pool_create(&launcher->pool, shm, 4096, launcher->globals.buffer_format);
	shm_pool_alloc_buffer(&launcher->pool, sizeof(struct taiwins_decision_key), NUM_DECISIONS);
	//we don't have the output here, we should have the launcher at the focused output
	appsurface_init(&launcher->launcher_surface, NULL, APP_WIDGET, launcher->globals.compositor, NULL);
	appsurface_init_input(&launcher->launcher_surface, key_handler, NULL, NULL, NULL);
	appsurface_init_buffer(&launcher->launcher_surface, &launcher->pool, &bounding);
//	void *buffer0 = shm_pool_buffer_access(appsurf->wl_buffer[0]);
//	void *buffer1 = shm_pool_buffer_access(appsurf->wl_buffer[1]);
}


//fuck, I wish that I have c++
static void
update_app_config(void *data,
		  struct taiwins_launcher *taiwins_launcher,
		  const char *app_name,
		  uint32_t floating,
		  wl_fixed_t scale)
{
//we don't nothing here now
}



struct taiwins_launcher_listener launcher_impl = {
	.application_configure = update_app_config,
};


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;

	if (strcmp(interface, taiwins_launcher_interface.name) == 0) {
		fprintf(stderr, "launcher registé\n");
		launcher->interface = (struct taiwins_launcher *)
			wl_registry_bind(wl_registry, name, &taiwins_launcher_interface, version);
		taiwins_launcher_add_listener(launcher->interface, &launcher_impl, launcher);
	} else
		wl_globals_announce(&launcher->globals, wl_registry, name, interface, version);
}


static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};


static void
desktop_launcher_init(struct desktop_launcher *launcher, struct wl_display *wl_display)
{
	wl_globals_init(&launcher->globals, wl_display);
	launcher->interface = NULL;
	launcher->quit = false;
	wl_display_set_user_data(wl_display, launcher);
}


static void
desktop_launcher_release(struct desktop_launcher *launcher)
{
	taiwins_launcher_destroy(launcher->interface);
	wl_globals_release(&launcher->globals);
	launcher->quit = true;
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}



int
main(int argc, char *argv[])
{
	struct desktop_launcher onelauncher;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to display\n");
		return -1;
	}
	desktop_launcher_init(&onelauncher, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &onelauncher);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	ready_the_launcher(&onelauncher);
	//okay, now we should create the buffers
	//event loop
	while(wl_display_dispatch(display) != -1 && !onelauncher.quit);
	desktop_launcher_release(&onelauncher);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}


//draw the text here
static void
_draw(struct desktop_launcher *launcher)
{
	//1) create the cairo_resource
	//2) determine the dimension
	void *data;
	int pixel_size;
	float r, g, b, left, caret;
	cairo_text_extents_t extend;
	struct app_surface *surf = &launcher->launcher_surface;
	cairo_format_t pixel_format;
	cairo_surface_t *cairo_surf;
	cairo_t *cr;


	if (surf->committed[1])
		return;
	data = shm_pool_buffer_access(surf->wl_buffer[1]);
	pixel_format = translate_wl_shm_format(launcher->globals.buffer_format);
	cairo_surf =
		cairo_image_surface_create_for_data((unsigned char *)data, pixel_format, surf->w, surf->h,
						    cairo_format_stride_for_width(pixel_format, surf->w));
	cr = cairo_create(cairo_surf);
	//clean the background
	wl_globals_bg_color_rgb(&launcher->globals, &r, &g, &b);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_paint(cr);
	wl_globals_fg_color_rgb(&launcher->globals, &r, &g, &b);
	cairo_set_source_rgb(cr, r, g, b);
	pixel_size = surf->h;
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, pixel_size);
	cairo_text_extents(cr, launcher->chars, &extend);
	caret = extend.height / 2;
	//you also need to know the size of the caret...
	//draw the text with the caret, we may gonna have a cairo library just in case.
	if (extend.width + 1.5 * caret < surf->w)
		left = 0.0;
	else {
		left = surf->w - extend.width - 1.5 * caret;
	}
	cairo_move_to(cr, left, (surf->h + extend.height) / 2);
	cairo_show_text(cr, launcher->chars);
	cairo_move_to(cr, left + extend.width + caret, (surf->h - extend.height)/2);
	cairo_line_to(cr, left + extend.width + caret, (surf->h + extend.height)/2);
	cairo_stroke(cr);
	//okay, now we can clean the pieces
	cairo_destroy(cr);
	cairo_surface_destroy(cairo_surf);
	//commit the buffer
}


static int
handle_ctrl_bindings(xkb_keysym_t keysym)
{
	//we are gonna need data_device, shit...
	switch (keysym) {
	case XKB_KEY_x:
	case XKB_KEY_w:
		//couper
		break;
	case XKB_KEY_c:
		//copier
		break;
	case XKB_KEY_y:
	case XKB_KEY_v:
		//coller
		break;
	case XKB_KEY_a:
		//moving to begining
		break;
	case XKB_KEY_e:
		//moving to the end;
		break;

	default:
		return 0;
	}
	return 1;
}

static int
handle_alt_bindings(xkb_keysym_t keysym)
{
	switch (keysym) {
	case XKB_KEY_w:
		//copier
		break;
	default:
		return 0;
	}
	return 1;
}


void
key_handler(struct app_surface *surf, xkb_keysym_t keysym, uint32_t modifier)
{
	//deal with modifiers
	switch (modifier) {
	case TW_CTRL:
		handle_ctrl_bindings(keysym);
		break;
	case TW_ALT:
		handle_alt_bindings(keysym);
		break;
	}

	switch (keysym) {
		//firstly, we don't do anything for the modifiers
	case XKB_KEY_Shift_L:
	case XKB_KEY_Shift_R:
	case XKB_KEY_Control_L:
	case XKB_KEY_Control_R:
	case XKB_KEY_Alt_L:
	case XKB_KEY_Alt_R:
	case XKB_KEY_Meta_L:
	case XKB_KEY_Meta_R:
		break;

	case XKB_KEY_Tab:
		//search
		break;
	case XKB_KEY_UP:
		//do the same thing as last command
		break;

	}
}

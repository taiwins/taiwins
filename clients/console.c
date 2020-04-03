/*
 * console.c - taiwins client console implementation
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-client.h>
#include <wayland-taiwins-theme-client-protocol.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-taiwins-console-client-protocol.h>

#include <os/exec.h>
#include <client.h>
#include <ui.h>
#include <shmpool.h>
#include <rax.h>
#include <strops.h>
#include <sequential.h>
#include <nk_backends.h>
#include <theme.h>
#include "../shared_config.h"

#include "common.h"
#include "console_module/console_module.h"
#include "vector.h"

struct selected_search_entry {
	console_search_entry_t entry;
	struct console_module *module;
};

struct desktop_console {
	struct taiwins_console *interface;
	struct taiwins_theme *theme_interface;
	struct taiwins_ui *proxy;
	struct tw_globals globals;
	struct tw_appsurf surface;
	struct tw_shm_pool pool;
	struct wl_buffer *decision_buffer;
	struct nk_wl_backend *bkend;
	struct wl_callback *exec_cb;
	uint32_t exec_id;

	off_t cursor;
	char chars[256];
	bool quit;
	//a good hack is that this text_edit is stateless, we don't need to
	//store anything once submitte
	struct nk_text_edit text_edit;

	vector_t modules;
	vector_t search_results; //search results from modules moves to here
	struct selected_search_entry selected;
	char *exec_result;

	struct tw_theme theme;
};

static void
submit_console(struct tw_appsurf *surf)
{
	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);
	taiwins_console_submit(console->interface, console->decision_buffer, console->exec_id);
	//and also do the exec.
	taiwins_ui_destroy(console->proxy);
	console->proxy = NULL;
	tw_appsurf_release(&console->surface);
}

static void
issue_commands(struct desktop_console *console,
	       const struct nk_str *str)
{
	struct console_module *module = NULL;
	char command[256] = {0};
	//careful here, cannot copy the all the command
	strop_ncpy(command, (char *)str->buffer.memory.ptr,
		MIN(256, str->len+1));
	vector_for_each(module, &console->modules)
		console_module_command(module, command, NULL);
}

static void
search_res_header(struct nk_context *ctx, const char *title)
{
    /* nk_style_set_font(ctx, &media->font_18->handle); */
    nk_layout_row_dynamic(ctx, 20, 1);
    /* struct nk_vec2 pos = nk_widget_position(ctx); */
    /* fprintf(stderr, "pos: (%f, %f)\n", pos.x, pos.y); */
    nk_label(ctx, title, NK_TEXT_LEFT);
}

static int
search_res_widget(struct nk_context *ctx, float height,
		  const console_search_entry_t *entry)
{
    static const float ratio[] = {0.15f, 0.85f};
    const char *str = search_entry_get_string(entry);
    /* nk_style_set_font(ctx, &media->font_22->handle); */
    nk_layout_row(ctx, NK_DYNAMIC, height, 2, ratio);
    nk_spacing(ctx, 1);
    /* struct nk_vec2 pos = nk_widget_size(ctx); */
    /* fprintf(stderr, "bounds: (%f, %f)\n", pos.x, pos.y); */
    return nk_button_label(ctx, str);
}


static bool
select_search_results(struct nk_context *ctx,
		      struct desktop_console *console,
		      struct selected_search_entry *selected)
{
	//we are drawing search results row by row.
	//there are a few things we need to do here:
	//1: offers user the ability to select the search results.
	//2: show the search results correctly (offseting the scrollbar manually)
	//3: search completetion and sorting, we can do neither of those now.
	bool complete = false;

	nk_layout_row_dynamic(ctx, 200, 1);
	nk_group_begin(ctx, "search_result", NK_WINDOW_SCALABLE);
	for (int i = 0; i < console->modules.len; i++) {
		console_search_entry_t *entry = NULL;
		vector_t *result =
			vector_at(&console->search_results, i);
		struct console_module *module =
			vector_at(&console->modules, i);
		//update the results if there is new stuff
		int errcode = console_module_take_search_result(module, result);
		if (errcode || !result->len)
			continue;

		search_res_header(ctx, module->name);
		vector_for_each(entry, result) {
			if (search_res_widget(ctx, 30, entry)) {
				search_entry_assign(&selected->entry, entry);
				selected->module = module;
				complete = true;
			}
		}
	}
	nk_group_end(ctx);
	return complete;
}

/**
 * @brief nuklear draw calls
 */
static void
draw_console(struct nk_context *ctx, float width, float height,
	     struct tw_appsurf *surf)
{
	enum EDITSTATE {NORMAL, SUBMITTING};
	static enum EDITSTATE edit_state = NORMAL;
	static char previous_tab[256] = {0};
	bool completion = false;
	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);
	struct selected_search_entry *selected = &console->selected;

	nk_layout_row_static(ctx, 30, width, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &console->text_edit,
		       nk_filter_default);
	//issue commands only in key done state
	if (nk_wl_get_keyinput(ctx) != XKB_KEY_NoSymbol)
		issue_commands(console, &console->text_edit.string);
	completion = select_search_results(ctx, console, selected);
	if (completion) {
		const char *line =
			search_entry_get_string(&selected->entry);
		nk_textedit_delete(&console->text_edit, 0, console->text_edit.string.len);
		nk_textedit_text(&console->text_edit, line, strlen(line));
	}
	if (nk_wl_get_keyinput(ctx) == XKB_KEY_NoSymbol) //key up
		return;

	/* if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER) && */
	/*     nk_input_is_key_pressed(&ctx->input, NK_KEY_SHIFT)) { */
	/*	//return and no closing */
	/* } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER)) { */
	/*	//return and closing */
	/* } */

	if (nk_wl_get_keyinput(ctx) == XKB_KEY_Return)
		edit_state = SUBMITTING;
	else
		edit_state = NORMAL;

	switch (edit_state) {
	case SUBMITTING:
		memset(previous_tab, 0, sizeof(previous_tab));
		edit_state = NORMAL;
		nk_wl_add_idle(ctx, submit_console);
		break;
	case NORMAL:
		memset(previous_tab, 0, sizeof(previous_tab));
		break;
	}
}

/*******************************************************************************
 * taiwins_console_interface
 ******************************************************************************/

static void
update_app_config(void *data,
		  struct taiwins_console *tw_console,
		  const char *app_name,
		  uint32_t floating,
		  wl_fixed_t scale)
{}

static void
start_console(void *data, struct taiwins_console *tw_console,
	      wl_fixed_t width, wl_fixed_t height, wl_fixed_t scale)
{
	struct desktop_console *console = (struct desktop_console *)data;
	struct tw_appsurf *surface = &console->surface;
	struct wl_surface *wl_surface = NULL;
	int w = wl_fixed_to_int(width);
	int h = wl_fixed_to_int(height);

	wl_surface = wl_compositor_create_surface(console->globals.compositor);
	console->proxy = taiwins_console_launch(tw_console, wl_surface);

	tw_appsurf_init(surface, wl_surface,
			 &console->globals, TW_APPSURF_WIDGET,
			 TW_APPSURF_NORESIZABLE);
	surface->tw_globals = &console->globals;
	nk_cairo_impl_app_surface(surface, console->bkend, draw_console,
				tw_make_bbox_origin(w, h, 1));
	tw_appsurf_frame(surface, false);
}

static void
exec_application(void *data, struct taiwins_console *tw_console, uint32_t id)
{
	struct desktop_console *console = data;
	struct selected_search_entry *selected =
		(console->selected.module) &&
		!search_entry_empty(&console->selected.entry) ?
		&console->selected : NULL;

	if (id != console->exec_id) {
		fprintf(stderr, "exec order not consistant, something wrong.");
	} else if (selected){
		console_module_command(selected->module, NULL,
				       search_entry_get_string(&selected->entry));
		free_console_search_entry(&selected->entry);
		*selected = (struct selected_search_entry){0};
		//parsing the input and command buffer. Then do it
		/* fork_exec(1, forks); */
	}
	nk_textedit_init_fixed(&console->text_edit, console->chars, 256);

	console->exec_id++;
}

struct taiwins_console_listener console_impl = {
	.application_configure = update_app_config,
	.start = start_console,
	.exec = exec_application,
};

/*******************************************************************************
 * taiwins_theme_interface
 ******************************************************************************/

static void
console_apply_theme(void *data,
                    struct taiwins_theme *taiwins_theme,
                    const char *name,
                    int32_t fd,
                    uint32_t size)
{
	struct desktop_console *console = data;

	tw_theme_fini(&console->theme);
	tw_theme_init_from_fd(&console->theme, fd, size);
}


static const struct taiwins_theme_listener theme_impl = {
	.theme = console_apply_theme,
};

/*******************************************************************************
 * desktop_console_interface
 ******************************************************************************/

static void
console_release_module(void *m)
{
	struct console_module *module = m;
	console_module_release(module);
}

static void
console_free_search_results(void *m)
{
	vector_t *v = m;
	vector_destroy(v);
}

static void
free_defunct_app(int signum)
{
	int wstatus;
	wait(&wstatus);
}

static void
post_init_console(struct desktop_console *console)
{
	tw_shm_pool_init(&console->pool, console->globals.shm,
	                 TAIWINS_CONSOLE_CONF_NUM_DECISIONS *
	                 sizeof(struct tw_decision_key),
		      console->globals.buffer_format);
	console->decision_buffer =
		tw_shm_pool_alloc_buffer(&console->pool,
		                         sizeof(struct tw_decision_key),
		                         TAIWINS_CONSOLE_CONF_NUM_DECISIONS);
	//prepare backend
	console->bkend = nk_cairo_create_backend();
	nk_textedit_init_fixed(&console->text_edit, console->chars, 256);

	//loading modules
	struct console_module *module;
	vector_t empty_res = {0};
	// init modules
	vector_init(&console->modules, sizeof(struct console_module),
		    console_release_module);
	//adding modules
	vector_append(&console->modules, &app_module);
	vector_append(&console->modules, &cmd_module);
	vector_for_each(module, &console->modules)
		console_module_init(module, console); //thread created

	vector_init(&console->search_results, sizeof(vector_t),
		    console_free_search_results);
	vector_for_each(module, &console->modules)
		vector_append(&console->search_results, &empty_res);
	console->selected = (struct selected_search_entry){0};
}
static void
init_console(struct desktop_console *console)
{
	signal(SIGCHLD, free_defunct_app);
	memset(console->chars, 0, sizeof(console->chars));
	console->quit = false;

	tw_theme_init_default(&console->theme);
	console->globals.theme = &console->theme;

}

static void
end_console(struct desktop_console *console)
{
	nk_textedit_free(&console->text_edit);
	nk_cairo_destroy_backend(console->bkend);
	tw_shm_pool_release(&console->pool);

	taiwins_console_destroy(console->interface);
	tw_globals_release(&console->globals);

	vector_destroy(&console->modules);
	vector_destroy(&console->search_results);

	console->quit = true;
}

/*******************************************************************************
 * globals
 ******************************************************************************/

static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_console *console = (struct desktop_console *)data;

	if (strcmp(interface, taiwins_console_interface.name) == 0) {
		fprintf(stderr, "console registé\n");
		console->interface = (struct taiwins_console *)
			wl_registry_bind(wl_registry, name,
			                 &taiwins_console_interface, version);
		taiwins_console_add_listener(console->interface,
		                             &console_impl, console);

	} else if (strcmp(interface, taiwins_theme_interface.name) == 0) {
		console->theme_interface = (struct taiwins_theme *)
			wl_registry_bind(wl_registry, name,
			                 &taiwins_theme_interface, version);
		taiwins_theme_add_listener(console->theme_interface,
		                           &theme_impl, console);

	} else
		tw_globals_announce(&console->globals, wl_registry, name,
		                    interface, version);
}


static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};


int
main(int argc, char *argv[])
{
	if (!create_cache_dir())
		return -1;

	struct desktop_console tw_console;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to display\n");
		return -1;
	}
	tw_globals_init(&tw_console.globals, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &tw_console);

	init_console(&tw_console);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	post_init_console(&tw_console);

	wl_display_flush(display);
	tw_globals_dispatch_event_queue(&tw_console.globals);
	end_console(&tw_console);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}

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
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
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
#include <helpers.h>
#include <strops.h>
#include <sequential.h>
#include <nk_backends.h>
#include <theme.h>
#include "../shared_config.h"

#include "common.h"
#include "console_module/console_module.h"

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

	char cmds[256];
	char prev_cmds[256];
	int prev_cmds_len;
	//a good hack is that this text_edit is stateless, we don't need to
	//store anything once submitte
	struct nk_text_edit text_edit;

	vector_t modules;
	/**< search results from modules, same length as modules */
	vector_t search_results;
	struct selected_search_entry selected;
	char *exec_result;

	struct tw_theme theme;
};

static void
console_do_submit(struct tw_appsurf *surf)
{
	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);
	taiwins_console_submit(console->interface, console->decision_buffer,
	                       console->exec_id);
	// and also do the exec.
	taiwins_ui_destroy(console->proxy);
	console->proxy = NULL;
	tw_appsurf_release(&console->surface);
}

static void
console_issue_commands(struct desktop_console *console,
                       const struct nk_str *str)
{
	struct console_module *module = NULL;
	char command[256] = {0};
	// careful here, cannot copy the all the command
	strop_ncpy(command, (char *)str->buffer.memory.ptr,
	           MIN(256, str->len + 1));
	vector_for_each(module, &console->modules)
		console_module_command(module, command, NULL);
}

static inline bool
console_has_selected(struct desktop_console *console)
{
	return console->selected.module &&
		!search_entry_empty(&console->selected.entry);
}

static inline void
console_clear_selected(struct desktop_console *console)
{
	console->selected.module = NULL;
	console->selected.entry.pstr = NULL;
	console->selected.entry.sstr[0] = '\0';
}

static inline void
console_clear_edit(struct desktop_console *console)
{
	memset(console->cmds, 0, NUMOF(console->cmds));
	memset(console->prev_cmds, 0, NUMOF(console->prev_cmds));
	nk_textedit_init_fixed(&console->text_edit, console->cmds,
	                       NUMOF(console->cmds));
}

static bool
console_edit_changed(struct desktop_console *console, int *prev_len)
{
	int len = console->text_edit.string.len;
	bool changed = (*prev_len != len) ||
		memcmp(console->cmds, console->prev_cmds,
		       MIN((size_t)len, NUMOF(console->cmds))) != 0;

	if (changed)
		memcpy(console->prev_cmds, console->cmds,
		       NUMOF(console->cmds));
	*prev_len = console->text_edit.string.len;
	return changed;
}

static inline void
console_update_search_results(struct desktop_console *console)
{
	struct console_module *module;
	vector_t *results;
	for (int i = 0; i < console->modules.len; i++) {
		module = vector_at(&console->modules, i);
		results = vector_at(&console->search_results, i);
		console_module_take_search_result(module, results);
	}
}

static bool
console_select_default(struct desktop_console *console)
{
	bool updated = false;
	struct console_module *module;
	console_search_entry_t *entry;
	vector_t *result;
	//for every module, in the list, find the the
	// first elements is the
	for (int i = 0; i < console->modules.len; i++) {
		result = vector_at(&console->search_results, i);
		module = vector_at(&console->modules, i);

		if (!result->len)
			continue;

		vector_for_each(entry, result) {
			search_entry_assign(&console->selected.entry, entry);
			console->selected.module = module;
			updated = true;
			break;
		}
		break;
	}
	return updated;
}

static inline void
console_update_selected(struct desktop_console *console)
{
	if (console_has_selected(console))
		return;

	console_select_default(console);
}

static void
console_handle_tab(struct desktop_console *console)
{
	const char *line;

	if (console_has_selected(console) ||
	    console_select_default(console)) {
		line = search_entry_get_string(&console->selected.entry);
		nk_textedit_delete(&console->text_edit, 0,
		                   console->text_edit.string.len);
		nk_textedit_text(&console->text_edit, line, strlen(line));
	}
}

static void
console_handle_nav(struct desktop_console *console, bool up)
{
	console_search_entry_t *prev, *curr, *next;
	struct console_module *module;
	struct console_module *prev_module, *curr_module, *next_module;
	console_search_entry_t *entry;
	vector_t *result;
	bool selected = false;

	prev = curr = next = NULL;
	prev_module = curr_module = next_module = NULL;

	for (int i = 0; i < console->modules.len; i++) {
		result = vector_at(&console->search_results, i);
		module = vector_at(&console->modules, i);

		if (!result->len)
			continue;
		// updating prev, curr, next
		vector_for_each(entry, result) {
			if (selected) {
				next = entry;
				next_module = module;
				break;
			} else {
				if (search_entry_equal(&console->selected.entry,
				                       entry))
					selected = true;
				prev = curr;
				curr = entry;
				prev_module = curr_module;
				curr_module = module;
			}
		}
		if (next)
			break;
	}
	// handling selected is first or last elem.
	if (!prev)
		prev = curr;
	if (!next)
		next = curr;
	if (!prev_module)
		prev_module = curr_module;
	if (!next_module)
		next_module = curr_module;

	if (up) {
		console->selected.module = prev_module;
		search_entry_assign(&console->selected.entry, prev);
	} else {
		console->selected.module = next_module;
		search_entry_assign(&console->selected.entry, next);
	}

}

/*******************************************************************************
 * console drawing functions
 ******************************************************************************/

static void
draw_module_results(struct nk_context *ctx, struct desktop_console *console,
            struct console_module *module, vector_t *results)
{
	console_search_entry_t *entry = NULL;
	int selected = 0;
	int clicked = 0;
	struct nk_rect bound;
	static const float ratio[] = {0.15f, 0.85f};

	vector_for_each(entry, results) {
		selected = (module == console->selected.module) &&
			search_entry_equal(&console->selected.entry,
			                   entry);
		if (selected)
			fprintf(stdout, "%s is selected\n",
			        search_entry_get_string(entry));

		nk_layout_row(ctx, NK_DYNAMIC, 20,  2, ratio);
		nk_spacing(ctx, 1);
		bound = nk_widget_bounds(ctx);
		nk_selectable_label(ctx, search_entry_get_string(entry),
		                    NK_TEXT_CENTERED, &selected);
		clicked = nk_input_is_mouse_click_in_rect(&ctx->input,
		                                          NK_BUTTON_LEFT,
		                                          bound);
		if (clicked) {
			search_entry_assign(&console->selected.entry, entry);
			console->selected.module = module;
		}
	}

}

static void
draw_search_results(struct nk_context *ctx, struct desktop_console *console)
{
	vector_t *results;
	struct console_module *module;

	nk_layout_row_dynamic(ctx, 200, 1);
	if (nk_group_begin(ctx, "search_result", NK_WINDOW_SCALABLE)) {
		for (int i = 0; i < console->modules.len; i++) {
			module = vector_at(&console->modules, i);
			results = vector_at(&console->search_results, i);
			//header
			nk_layout_row_dynamic(ctx, 20, 1);
			nk_label(ctx, module->name, NK_TEXT_LEFT);
			//module results
			draw_module_results(ctx, console, module, results);
		}
		nk_group_end(ctx);
	}
}

static int
console_edit_filter(const struct nk_text_edit *box, nk_rune unicode)
{
	NK_UNUSED(box);

	//TODO: deal with things like backspace
	if (isprint(unicode) && !isspace(unicode))
		return nk_true;
	else
		return nk_false;
}

/**
 * @brief nuklear draw calls
 */
static void
draw_console(struct nk_context *ctx, float width, float height,
	     struct tw_appsurf *surf)
{
	static int prev_cmd_len = 0;
        bool submit = false, disable_clearing = false;
        struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);

        NK_UNUSED(height);
	console_update_search_results(console);
	console_update_selected(console);
	//we shall get our default selection here...

	if (nk_input_is_key_pressed(&ctx->input, NK_KEY_TAB)) {
		console_handle_tab(console);
                disable_clearing = true;
	} else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER)) {
		submit = true;
		disable_clearing = true;
	} else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_UP)) {
		console_handle_nav(console, true);
		disable_clearing = true;
	} else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN)) {
		console_handle_nav(console, false);
		disable_clearing = true;
	}

	nk_layout_row_static(ctx, 30, width, 1);
	nk_edit_focus(ctx, 0);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &console->text_edit,
	               console_edit_filter);

	if (console_edit_changed(console, &prev_cmd_len)) {
          console_issue_commands(console, &console->text_edit.string);
          if (!disable_clearing)
            console_clear_selected(console);
	}
	//we need to get here as well
	draw_search_results(ctx, console);

	if (submit) {
          nk_wl_add_idle(ctx, console_do_submit);
          prev_cmd_len = 0;
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
exec_application(void *data, struct taiwins_console *protocol, uint32_t id)
{
	struct desktop_console *console = data;
	struct selected_search_entry *selected =
		console_has_selected(console) ? &console->selected : NULL;

	if (id != console->exec_id) {
		fprintf(stderr, "exec order not consistant, something wrong.");
	} else if (selected){
		console_module_command(selected->module, NULL,
				       search_entry_get_string(&selected->entry));
		search_entry_free(&selected->entry);
		selected->module = NULL;
	}
	console_clear_edit(console);
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

static void
console_apply_cursor(void *data,
                     struct taiwins_theme *taiwins_theme,
                     const char *name,
                     uint32_t size)
{
	struct desktop_console *console = data;

	strncpy(console->globals.inputs.cursor_theme_name, name, 63);
	console->globals.inputs.cursor_size = size;

	tw_globals_reload_cursor_theme(&console->globals);
}


static const struct taiwins_theme_listener theme_impl = {
	.theme = console_apply_theme,
	.cursor = console_apply_cursor,
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
	int pid, status;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		fprintf(stderr, "defunct child exit!\n");
		continue;
	}
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
	console_clear_edit(console);

	//loading modules
	struct console_module *module;
	vector_t empty_res = {0};
	// init modules
	vector_init(&console->modules, sizeof(struct console_module),
		    console_release_module);
	//adding default modules
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

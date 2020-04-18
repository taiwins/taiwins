/*
 * console.c - taiwins client console implementation
 *
 * Copyright (c) 2019-2020 Xichen Zhou
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
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>
#include <wayland-taiwins-theme-client-protocol.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include <wayland-taiwins-console-client-protocol.h>

#include <client.h>
#include <ui.h>
#include <nk_backends.h>
#include <theme.h>
#include <shmpool.h>
#include <helpers.h>
#include <strops.h>
#include <sequential.h>
#include "../shared_config.h"

#include "common.h"
#include "console_module/console_module.h"
#include "event_queue.h"
#include "ui_event.h"
#include "vector.h"


#define CON_EDIT_H 30
#define CON_ENTY_H 24
#define CON_MOD_GAP 4

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
	struct nk_text_edit text_edit;

	vector_t modules;
	/**< search results from modules, same length as modules */
	vector_t search_results;
	struct selected_search_entry selected;
	char *exec_result;

	struct tw_theme theme;
	struct tw_bbox collapsed_bounds;
	struct tw_bbox bounds;
};

static int
console_do_submit(struct tw_event *e, int fd)
{
	struct tw_appsurf *surf = e->data;
	struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);

	taiwins_console_submit(console->interface, console->decision_buffer,
	                       console->exec_id);
	// and also do the exec.
	taiwins_ui_destroy(console->proxy);
	console->proxy = NULL;
	tw_appsurf_release(&console->surface);
	return TW_EVENT_DEL;
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

static inline bool
console_update_search_results(struct desktop_console *console)
{
	bool has_results = false, clean_results = false;
	struct console_module *module;
	vector_t *results;
	if (console->text_edit.string.len == 0)
		clean_results = true;

	for (int i = 0; i < console->modules.len; i++) {
		module = vector_at(&console->modules, i);
		results = vector_at(&console->search_results, i);
		if (clean_results)
			vector_destroy(results);
		else
			console_module_take_search_result(module, results);
		if (results->len > 0)
			has_results = true;
	}
	return has_results;
}

static bool
console_select_default(struct desktop_console *console)
{
	bool updated = false;
	struct console_module *module;
	console_search_entry_t *entry;
	vector_t *result;

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
	// handling edge case.
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

static int
console_resize(struct tw_event *e, int fd)
{
	struct tw_appsurf *console_surf = e->data;
	uint32_t nh = e->arg.u;

	(void)fd;
	tw_appsurf_resize(console_surf,
	                  console_surf->allocation.w,
	                  nh,
	                  console_surf->allocation.s);

	return TW_EVENT_DEL;
}

/*******************************************************************************
 * console drawing functions
 ******************************************************************************/

static bool
console_draw_module_results(struct nk_context *ctx,
                            struct desktop_console *console,
                            struct console_module *module, vector_t *results,
                            struct nk_rect *selected_bound)
{
	bool ret = false;
	console_search_entry_t *entry = NULL;
	int selected = 0;
	int clicked = 0;
	struct nk_rect bound;
	static const float ratio[] = {0.30, 0.70f};
	bool draw_module_name = false;

	vector_for_each(entry, results) {
		selected = (module == console->selected.module) &&
			search_entry_equal(&console->selected.entry,
			                   entry);

		nk_layout_row(ctx, NK_DYNAMIC, CON_ENTY_H, 2, ratio);
		//draw module title
		if (!draw_module_name) {
			nk_label(ctx, module->name, NK_TEXT_LEFT);
			draw_module_name = true;
		} else
			nk_spacing(ctx, 1);
		//draw the actual widget
		bound = nk_widget_bounds(ctx);
		nk_selectable_label(ctx, search_entry_get_string(entry),
		                    NK_TEXT_CENTERED, &selected);
		clicked = nk_input_is_mouse_click_in_rect(&ctx->input,
		                                          NK_BUTTON_LEFT,
		                                          bound);
		if (selected)
			*selected_bound = bound;
		if (clicked) {
			search_entry_assign(&console->selected.entry, entry);
			console->selected.module = module;
			ret = true;
		}
	}
	return ret;
}

static void
console_calc_scroll(const struct nk_rect *selected,
                    const struct nk_rect *bound, struct nk_scroll *scroll)
{
	int yoffset = (int)scroll->y;
	int top_diff = (int)selected->y - (int)bound->y ;
	int bottom_diff = (int)(selected->y + selected->h) -
		(int)(bound->y + bound->h);

	if (top_diff < 0)
		yoffset = MAX(0, yoffset + top_diff);
	else if (bottom_diff > 0)
		yoffset += bottom_diff;

	scroll->y = yoffset;
}

static bool
console_draw_search_results(struct nk_context *ctx,
                            struct desktop_console *console)
{
	bool clicked = false;
	vector_t *results;
	struct nk_style_window *style = &ctx->style.window;
	struct console_module *module;
	struct nk_rect selected_bound = {0};
	struct nk_rect results_bound;
	static struct nk_scroll offset = {0, 0};
	nk_flags flags = NK_WINDOW_NO_SCROLLBAR;

	//calculate bound
	nk_layout_row_dynamic(ctx, 240, 1);
	results_bound = nk_widget_bounds(ctx);

	if (nk_group_scrolled_begin(ctx, &offset, "search_result", flags)) {
		for (int i = 0; i < console->modules.len; i++) {
			module = vector_at(&console->modules, i);
			results = vector_at(&console->search_results, i);
			//header
			nk_layout_row_dynamic(ctx, CON_MOD_GAP, 1);
			nk_spacing(ctx, 1);

			//module results
			if (console_draw_module_results(ctx, console, module,
			                        results, &selected_bound))
				clicked = true;
		}
		nk_group_scrolled_end(ctx);
	}
	results_bound.h -= style->padding.y + style->spacing.y;
	console_calc_scroll(&selected_bound, &results_bound, &offset);

	return clicked;
}

static int
console_edit_filter(const struct nk_text_edit *box, nk_rune unicode)
{
	NK_UNUSED(box);

	if (isprint(unicode) &&
	    (!isspace(unicode) || unicode == '\n' || unicode == ' '))
		return nk_true;
	else
		return nk_false;
}

static inline void
console_clean_nav_key(struct nk_context *ctx)
{
	ctx->input.keyboard.keys[NK_KEY_UP].down = 0;
	ctx->input.keyboard.keys[NK_KEY_UP].clicked = 0;
	ctx->input.keyboard.keys[NK_KEY_DOWN].down = 0;
	ctx->input.keyboard.keys[NK_KEY_DOWN].clicked = 0;
}

/**
 * @brief draw the console application
 */
static void
console_draw(struct nk_context *ctx, float width, float height,
	     struct tw_appsurf *surf)
{
	static int prev_cmd_len = 0;
        bool submit = false, disable_clearing = false;
        bool has_results = false;
        struct desktop_console *console =
		container_of(surf, struct desktop_console, surface);
        struct tw_event_queue *queue = &console->globals.event_queue;
        struct tw_event resize_event = {
	        .cb = console_resize,
	        .data = surf,
        };
        struct tw_event submit_event = {
	        .cb = console_do_submit,
	        .data = surf,
        };

	has_results = console_update_search_results(console);
	console_update_selected(console);

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
	console_clean_nav_key(ctx);

	// actual drawing part
	if (nk_begin(ctx, "taiwins_console", nk_rect(0, 0, width, height),
	             NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {

		nk_layout_row_dynamic(ctx, CON_EDIT_H, 1);
		nk_edit_focus(ctx, 0);
		nk_edit_buffer(ctx, NK_EDIT_FIELD, &console->text_edit,
		               console_edit_filter);

		if (console_edit_changed(console, &prev_cmd_len)) {
			console_issue_commands(console,
			                       &console->text_edit.string);
			if (!disable_clearing)
				console_clear_selected(console);
		}

		if (has_results && console_draw_search_results(ctx, console))
			submit = true;
	} nk_end(ctx);

	if (submit) {
		tw_event_queue_add_idle(queue, &submit_event);
		prev_cmd_len = 0;
	}
	if (has_results && (height != console->bounds.h)) {
		resize_event.arg.u = console->bounds.h;
		tw_event_queue_add_idle(queue, &resize_event);
	} else if (!has_results && (height != console->collapsed_bounds.h)) {
		resize_event.arg.u = console->collapsed_bounds.h;
		tw_event_queue_add_idle(queue, &resize_event);
	}
}

/*******************************************************************************
 * taiwins_console_filter
 ******************************************************************************/

static bool
suspend_console_on_esc(struct tw_appsurf *app, const struct tw_app_event *e)
{
	struct desktop_console *console =
		container_of(app, struct desktop_console, surface);
	struct tw_event_queue *queue = &console->globals.event_queue;
        struct tw_event submit_event = {
	        .cb = console_do_submit,
	        .data = app,
        };

	if (e->key.sym == XKB_KEY_Escape) {
		console_clear_selected(console);
		tw_event_queue_add_idle(queue, &submit_event);
		return true;
	} else
		return false;
}

static struct tw_app_event_filter console_key_filter = {
	.intercept = suspend_console_on_esc,
	.type = TW_KEY_BTN,
	.link.prev = &console_key_filter.link,
	.link.next = &console_key_filter.link,
};

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
	int s = wl_fixed_to_int(scale);
	int border = console->theme.window.border;
	int margin = console->theme.window.spacing.y;

	console->bounds = tw_make_bbox_origin(w, h, s);
	console->collapsed_bounds =
		tw_make_bbox_origin(w, CON_EDIT_H + border * 2 + margin * 2, s);

	wl_surface = wl_compositor_create_surface(console->globals.compositor);
	console->proxy = taiwins_console_launch(tw_console, wl_surface);

	tw_appsurf_init(surface, wl_surface,
	                &console->globals, TW_APPSURF_WIDGET,
	                TW_APPSURF_COMPOSITE);
	surface->tw_globals = &console->globals;
	wl_list_insert(&surface->filter_head, &console_key_filter.link);
	nk_cairo_impl_app_surface(surface, console->bkend, console_draw,
				console->collapsed_bounds);

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
		const char *cmd;
		const char *df_cmd = search_entry_get_string(&selected->entry);

		console->cmds[console->text_edit.string.len] = '\0';
		cmd = (strstr(console->cmds, df_cmd)) ? console->cmds : df_cmd;
		console_module_command(selected->module, NULL, cmd);

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

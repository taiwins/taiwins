/*
 * config.c - taiwins config functions
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include <linux/input.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <sequential.h>
#include <os/file.h>
#include <strops.h>
#include <libweston/libweston.h>

#include "config_internal.h"
#include "server/backend.h"
#include "server/bindings.h"
#include "server/compositor.h"
#include "server/desktop/desktop.h"
#include "server/desktop/shell.h"
#include "server/taiwins.h"
#include "vector.h"

/******************************************************************************
 * API
 *****************************************************************************/

struct tw_config*
tw_config_create(struct weston_compositor *ec, log_func_t log)
{
	struct tw_config *config =
		zalloc(sizeof(struct tw_config));
	config->err_msg = NULL;
	config->compositor = ec;
	config->print = log;
	config->quit = false;
	config->_config_time = false;
	config->bindings = tw_bindings_create(ec);
	config->config_table = tw_config_table_new(config);
	tw_config_default_bindings(config);
	vector_init_zero(&config->registry,
	                 sizeof(struct tw_config_obj), NULL);

	return config;
}

//release the config to be reused, we don't remove the apply bindings
//here. Since it maybe called again.
static inline void
_tw_config_release(struct tw_config *config)
{
	tw_bindings_destroy(config->bindings);
	tw_config_table_destroy(config->config_table);

	if (config->xkb_rules.layout)
		free((void *)config->xkb_rules.layout);
	if (config->xkb_rules.model)
		free((void *)config->xkb_rules.model);
	if (config->xkb_rules.options)
		free((void *)config->xkb_rules.options);
	if (config->xkb_rules.variant)
		free((void *)config->xkb_rules.variant);
	if (config->err_msg)
		free(config->err_msg);

	vector_destroy(&config->config_bindings);
}

void
tw_config_destroy(struct tw_config *config)
{
	_tw_config_release(config);
	free(config);
}

const char *
tw_config_retrieve_error(struct tw_config *config)
{
	return config->err_msg;
}

void
tw_config_register_object(struct tw_config *config,
                          const char *name, void *obj)
{
	struct tw_config_obj object;

	strop_ncpy((char *)object.name, name, 32);
	object.data = obj;
	vector_append(&config->registry, &object);
}

void *
tw_config_request_object(struct tw_config *config,
                         const char *name)
{
	struct tw_config_obj *obj;

	vector_for_each(obj, &config->registry) {
		if (!strcmp(name, obj->name))
			return obj->data;
	}
	return NULL;
}


/**
 * @brief swap all the config from one to another.
 *
 * at this point we know for sure we can apply the config. This works even if
 * our dst is a fresh new config. The release function will take care of freeing
 * things.
 */
static void
tw_swap_config(struct tw_config *dst, struct tw_config *src)
{
	_tw_config_release(dst);
	//clone everthing.
	dst->L = src->L;
	dst->config_table = src->config_table;
	dst->config_table->config = dst;
	dst->bindings = src->bindings;
	dst->config_bindings = src->config_bindings;
	dst->xkb_rules = src->xkb_rules;
	/* swap_listener(&dst->apply_bindings, &src->apply_bindings); */
	/* swap_listener(&dst->lua_components, &src->lua_components); */

	free(src);
}

static bool
tw_config_try_config(struct tw_config *tmp_config,
                     struct tw_config *main_config)
{
	char path[PATH_MAX];
	bool safe = true;
	struct tw_bindings *bindings;
	struct weston_compositor *ec = main_config->compositor;

	tw_create_config_dir();
	tw_config_dir(path);
	strcat(path, "/config.lua");
	if (main_config->err_msg)
		free(main_config->err_msg);

	tw_config_init_luastate(tmp_config);

        if (is_file_exist(path)) {
	        tmp_config->_config_time = true;
		safe = safe && !luaL_loadfile(tmp_config->L, path);
		safe = safe && !lua_pcall(tmp_config->L, 0, 0, 0);
		safe = safe && ec->state == WESTON_COMPOSITOR_ACTIVE;
		if (!safe) {
			main_config->err_msg =
				strdup(lua_tostring(tmp_config->L, -1));
		}
	}
        bindings = tmp_config->bindings;
        safe = safe && tw_config_install_bindings(main_config, bindings);

	return safe;
}

/**
 * @brief apply options we accumulated in the lua run
static void
tw_config_apply_cached(struct tw_config *config)
{
	if (config->xkb_rules.layout || config->xkb_rules.model ||
	    config->xkb_rules.options || config->xkb_rules.rules ||
	    config->xkb_rules.variant) {
		//this one should have runtime effect if weston finally took my patch
		weston_compositor_set_xkb_rule_names(config->compositor, &config->xkb_rules);
		config->xkb_rules = (struct xkb_rule_names){0};
	}
	if (config->kb_delay > 0 && config->kb_repeat) {
		config->compositor->kb_repeat_rate = config->kb_repeat;
		config->compositor->kb_repeat_delay = config->kb_delay;
		config->kb_delay = (config->kb_repeat = -1);
	}
}
*/

/**
 * @brief run/rerun the configurations.
 *
 * The configuration runs in protective mode. That is, it will create a
 * temporary configuration and run config script within. If everything is indeed
 * fine. We will migrate to the main config.
 */

bool
tw_run_config(struct tw_config *config)
{
	bool safe;
	struct tw_config *temp_config;

	temp_config = tw_config_create(config->compositor, config->print);

	temp_config->registry = config->registry;
	safe = tw_config_try_config(temp_config, config);
	vector_init_zero(&temp_config->registry,
	                 sizeof(struct tw_config_obj), NULL);

	if (safe) {
		tw_swap_config(config, temp_config);
		tw_bindings_apply(config->bindings);
		tw_config_table_flush(config->config_table);
	}
	//in either case, we would want to purge the temp config
	tw_config_destroy(temp_config);

	return safe;
}

bool
tw_run_default_config(struct tw_config *c)
{
	tw_config_wake_compositor(c);
	tw_bindings_apply(c->bindings);
	return true;
}

bool
tw_config_wake_compositor(struct tw_config *c)
{
	struct shell *shell;
	struct console *console;
	struct tw_backend *backend;
	struct tw_bus *bus;
	struct tw_theme *theme;
	struct tw_xwayland *xwayland;
	struct desktop *desktop;

	const char *shell_path;
	const char *console_path;
	struct tw_config_table *t = c->config_table;
	struct weston_compositor *ec = c->compositor;

        tw_config_table_flush(c->config_table);
	weston_compositor_set_xkb_rule_names(ec, &t->xkb_rules);
	ec->kb_repeat_rate = t->kb_repeat.val;
	ec->kb_repeat_delay = t->kb_delay.val;

	if (ec->state == WESTON_COMPOSITOR_ACTIVE)
		return true;
	weston_compositor_wake(ec);

        shell_path =  tw_config_request_object(c, "shell_path");
	console_path = tw_config_request_object(c, "console_path");

	//install bindings here?
	weston_compositor_wake(ec);

	if (!(backend = tw_setup_backend(ec)))
		goto out;
        tw_config_register_object(c, "backend", backend);

        if (!(bus = tw_setup_bus(ec)))
		goto out;
        tw_config_register_object(c, "bus", bus);

	if (!(shell = tw_setup_shell(ec, shell_path)))
		goto out;
        tw_config_register_object(c, "shell", shell);

        if (!(console = tw_setup_console(ec, console_path, shell)))
		goto out;
	tw_config_register_object(c, "console", console);

	if (!(desktop = tw_setup_desktop(ec, shell)))
		goto out;
	tw_config_register_object(c, "desktop", desktop);

	if (!(theme = tw_setup_theme(ec)))
		goto out;
	tw_config_register_object(c, "theme", theme);

	if (!(xwayland = tw_setup_xwayland(ec)))
		goto out;
        tw_config_register_object(c, "xwayland", xwayland);

        ec->default_pointer_grab = NULL;

out:
        return false;
}

const struct tw_binding *
tw_config_get_builtin_binding(struct tw_config *c,
				   enum tw_builtin_binding_t type)
{
	assert(type < TW_BUILTIN_BINDING_SIZE);
	return &c->builtin_bindings[type];
}

static void
tw_config_table_apply(void *data)
{
	struct tw_config_table *t = data;
	tw_config_table_flush(t);
}

struct tw_config_table *
tw_config_table_new(struct tw_config *c)
{
	struct tw_config_table *table =
		calloc(1, sizeof(struct tw_config_table));
	if (!table)
		return NULL;
	table->config = c;
	return table;
}

void
tw_config_table_destroy(struct tw_config_table *table)
{
	if (table->background_path.valid && table->background_path.path)
		free(table->background_path.path);
	if (table->widgets_path.valid && table->widgets_path.path)
		free(table->widgets_path.path);
	if (table->menu.valid && table->menu.vec.len)
		vector_destroy(&table->menu.vec);

        if (table->xkb_rules.layout)
		free((void *)table->xkb_rules.layout);
	if (table->xkb_rules.model)
		free((void *)table->xkb_rules.model);
	if (table->xkb_rules.options)
		free((void *)table->xkb_rules.options);
	if (table->xkb_rules.variant)
		free((void *)table->xkb_rules.variant);


	free(table);
}

void
tw_config_table_dirty(struct tw_config_table *t, bool dirty)
{
	struct wl_display *display;
	struct wl_event_loop *loop;

	if (t->config->_config_time || !dirty)
		return;
	display = t->config->compositor->wl_display;
	loop = wl_display_get_event_loop(display);

	wl_event_loop_add_idle(loop, tw_config_table_apply, t);
}

/* this function is the only point we apply for configurations, It can may run
 * in the middle of the configuration as well. For example, if lua config is
 * calling compositor.wake(). tw_config_table_apply would run and apply for the
 * configuration first before actually wakening the comositor.
*/
void
tw_config_table_flush(struct tw_config_table *t)
{
	struct tw_config *c = t->config;
	struct weston_compositor *ec = t->config->compositor;
	struct desktop *desktop;
	struct shell *shell;
	struct tw_xwayland *xwayland;
	struct tw_theme *theme;
	struct tw_backend *backend;
	enum tw_layout_type layout;

	desktop = tw_config_request_object(c, "desktop");
	shell  = tw_config_request_object(c, "shell");
	xwayland = tw_config_request_object(c, "xwayland");
	theme = tw_config_request_object(c, "theme");
	backend = tw_config_request_object(c, "backend");

	for (int i = 0; backend && i < 32 ; i++) {
		struct weston_output *output =
			 t->outputs[i].output;
		enum wl_output_transform transform =
			t->outputs[i].transform.transform;
		int32_t scale =
			t->outputs[i].scale.val;
		//this does not work for output already enabled actually.
		if (!output)
			continue;
		if (t->outputs[i].transform.valid) {
			weston_output_set_transform(output, transform);
			t->outputs[i].transform.valid = false;
		}
		if (t->outputs[i].scale.valid) {
			weston_output_set_scale(output, scale);
			t->outputs[i].scale.valid = false;
		}
		wl_signal_emit(&ec->output_resized_signal, output);
	}

        for (int i = 0; desktop && i < MAX_WORKSPACE; i++) {
		layout = t->workspaces[i].layout.layout;
		if (t->workspaces[i].layout.valid)
			tw_desktop_set_workspace_layout(desktop, i, layout);
		t->workspaces[i].layout.valid = false;
	}

        if (desktop && (t->desktop_igap.valid || t->desktop_ogap.valid)) {
		tw_desktop_set_gap(desktop,
		                   t->desktop_igap.val,
		                   t->desktop_ogap.val);
		t->desktop_igap.valid = false;
		t->desktop_ogap.valid = false;
	}

	if (xwayland && t->xwayland.valid) {
		tw_xwayland_enable(xwayland, t->xwayland.enable);
		t->xwayland.valid = false;
	}

	if (shell && t->background_path.valid) {
		tw_shell_set_wallpaper(shell, t->background_path.path);
		free(t->background_path.path);
		t->background_path.valid = false;
		t->background_path.path = NULL;
	}

        if (shell && t->widgets_path.valid) {
		tw_shell_set_widget_path(shell, t->widgets_path.path);
		free(t->widgets_path.path);
		t->widgets_path.valid = false;
		t->widgets_path.path = NULL;
	}

	if (shell && t->menu.valid) {
		tw_shell_set_menu(shell, &t->menu.vec);
		vector_init_zero(&t->menu.vec, 1, NULL);
		t->menu.valid = false;
	}

	if (shell && t->lock_timer.valid) {
		ec->idle_time = t->lock_timer.val;
		t->lock_timer.valid = false;
	}

	if (theme && t->theme.valid) {
		tw_theme_notify(theme);
		t->theme.read = false;
		t->theme.valid = false;
	}

	weston_compositor_schedule_repaint(ec);
}

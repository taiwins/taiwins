/*
 * config.c - taiwins config functions
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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include <linux/input.h>
#include <ctype.h>
#include <ctypes/sequential.h>
#include <ctypes/os/file.h>
#include <ctypes/strops.h>

#include "backend.h"
#include "shell.h"
#include "bindings.h"
#include "config_internal.h"
#include "taiwins/objects/utils.h"
#include "xdg.h"



static const uint32_t TW_CONFIG_GLOBAL_DEFAULT =
	TW_CONFIG_GLOBAL_BUS |
	TW_CONFIG_GLOBAL_TAIWINS_SHELL |
	TW_CONFIG_GLOBAL_TAIWINS_CONSOLE |
	TW_CONFIG_GLOBAL_TAIWINS_THEME |
	TW_CONFIG_GLOBAL_LAYER_SHELL |
	TW_CONFIG_GLOBAL_XWAYLAND |
	TW_CONFIG_GLOBAL_DESKTOP;

/******************************************************************************
 * API
 *****************************************************************************/

struct tw_config*
tw_config_create(struct tw_backend *backend)
{
	struct tw_config *config =
		calloc(1, sizeof(struct tw_config));
	if (!config)
		return NULL;

	config->backend = backend;
	config->err_msg = NULL;
	config->user_data = NULL;
	config->_config_time = false;
	config->bindings = tw_bindings_create(backend->display);
	config->config_table = tw_config_table_new(config);

	config->init = tw_luaconfig_init;
	config->fini = tw_luaconfig_fini;
	config->read_error = tw_luaconfig_read_error;
	config->read_config = tw_luaconfig_read;

	tw_config_default_bindings(config);

	vector_init_zero(&config->config_bindings,
	                 sizeof(struct tw_binding), NULL);
	vector_init_zero(&config->registry,
	                 sizeof(struct tw_config_obj), NULL);

	wl_list_init(&config->output_created_listener.link);
	wl_list_init(&config->output_destroyed_listener.link);
	return config;
}

static inline void
_tw_config_release(struct tw_config *config)
{
	if (config->bindings)
		tw_bindings_destroy(config->bindings);
	if (config->config_table)
		tw_config_table_destroy(config->config_table);
	if (config->err_msg)
		free(config->err_msg);
	config->fini(config);

	vector_destroy(&config->config_bindings);
	vector_destroy(&config->registry);

	wl_list_remove(&config->output_created_listener.link);
	wl_list_remove(&config->output_destroyed_listener.link);
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

static void
tw_config_copy_waken(struct tw_config *dst, struct tw_config *src)
{
	tw_config_register_object(dst, "backend",
		tw_config_request_object(src, "backend"));
	tw_config_register_object(dst, "bus",
		tw_config_request_object(src, "bus"));
	tw_config_register_object(dst, "shell",
		tw_config_request_object(src, "shell"));
	tw_config_register_object(dst, "console",
		tw_config_request_object(src, "console"));
	tw_config_register_object(dst, "desktop",
		tw_config_request_object(src, "desktop"));
	tw_config_register_object(dst, "theme",
		tw_config_request_object(src, "theme"));
	tw_config_register_object(dst, "xwayland",
		tw_config_request_object(src, "xwayland"));
	tw_config_register_object(dst, "initialized",
		tw_config_request_object(src, "initialized"));
}

void
tw_config_register_object(struct tw_config *config,
                          const char *name, void *obj)
{
	struct tw_config_obj object;
	struct tw_config_obj *find;

	vector_for_each(find, &config->registry) {
		if (!strcmp(find->name, name)) {
			find->data = obj;
			return;
		}
	}

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

static inline bool
copy_builtin_bindings(struct tw_binding *dst, const struct tw_binding *src)
{
	memcpy(dst, src, sizeof(struct tw_binding) * TW_BUILTIN_BINDING_SIZE);
	return true;
}

static inline void
copy_signals(struct tw_config *dst, const struct tw_config *src)
{
	struct tw_backend *backend = src->backend;
	dst->backend = src->backend;
	//add signals
	if (src->output_created_listener.notify) {
		tw_signal_setup_listener(&backend->output_plug_signal,
		                         &dst->output_created_listener,
		                         src->output_created_listener.notify);
	}
	if (src->output_destroyed_listener.notify) {
		tw_signal_setup_listener(&backend->output_unplug_signal,
		                         &dst->output_destroyed_listener,
		                         src->output_destroyed_listener.notify);
	}
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
	if (dst->user_data)
		dst->fini(dst);
	dst->user_data = src->user_data;
	dst->config_table = src->config_table;
	dst->config_table->config = dst;
	dst->bindings = src->bindings;
	dst->config_bindings = src->config_bindings;
	dst->registry = src->registry;
	copy_builtin_bindings(dst->builtin_bindings, src->builtin_bindings);
	copy_signals(dst, src);
	//reset src data
	src->bindings = NULL;
	src->config_table = NULL;
	src->user_data = NULL;
	vector_init_zero(&src->registry,
	                 sizeof(struct tw_config_obj), NULL);
	vector_init_zero(&src->config_bindings,
	                 sizeof(struct tw_binding), NULL);
}

static bool
tw_try_config(struct tw_config *tmp_config, struct tw_config *main_config)
{
	char path[PATH_MAX];
	bool safe = true;
	struct tw_bindings *bindings;
	struct tw_binding tmp[TW_BUILTIN_BINDING_SIZE];

	tw_create_config_dir();
	tw_config_dir(path);
	strcat(path, "/config.lua");
	if (main_config->err_msg)
		free(main_config->err_msg);
	tw_config_register_object(tmp_config, "shell_path",
	                          tw_config_request_object(main_config,
	                                                   "shell_path"));
	tw_config_register_object(tmp_config, "console_path",
	                          tw_config_request_object(main_config,
	                                                   "console_path"));
	tw_config_register_object(tmp_config, "initialized",
	                          tw_config_request_object(main_config,
	                                                   "initialized"));
	tmp_config->init(tmp_config);

	if (is_file_exist(path)) {
		tmp_config->_config_time = true;
		safe = safe && tmp_config->read_config(tmp_config, path);
		safe = safe && tw_config_request_object(tmp_config,
		                                        "initialized");
		//compositor could be waken by now, even if we had error in
		//configs.
		tw_config_copy_waken(main_config, tmp_config);
		if (!safe)
			main_config->err_msg =
				tmp_config->read_error(tmp_config);
	}
	bindings = tmp_config->bindings;
	copy_builtin_bindings(tmp, main_config->builtin_bindings);
	copy_builtin_bindings(main_config->builtin_bindings,
	                      tmp_config->builtin_bindings);
	safe = safe && tw_config_install_bindings(main_config, bindings);
	copy_builtin_bindings(main_config->builtin_bindings, tmp);

	return safe;
}

/**
 * @brief run/rerun the configurations.
 *
 * The configuration runs in protective mode. We create a temporary
 * configuration and run config script within. If everything is indeed fine. We
 * will migrate to the main config.
 */

bool
tw_run_config(struct tw_config *config)
{
	bool safe;
	struct tw_config *temp_config;

	temp_config = tw_config_create(config->backend);

	safe = tw_try_config(temp_config, config);

	if (safe) {
		tw_swap_config(config, temp_config);
		tw_config_table_flush(config->config_table);
	}
	//in either case, we would want to purge the temp config
	tw_config_destroy(temp_config);

	return safe;
}

bool
tw_run_default_config(struct tw_config *c)
{
	c->config_table->enable_globals = TW_CONFIG_GLOBAL_DEFAULT;
	tw_config_wake_compositor(c);
	tw_config_install_bindings(c, c->bindings);
	return true;
}

bool
tw_config_wake_compositor(struct tw_config *c)
{
	struct tw_shell *shell = NULL;
	struct tw_console *console;
	struct tw_bus *bus;
	struct tw_theme *theme;
	/* struct tw_xwayland *xwayland; */
	struct tw_xdg *xdg;
	struct tw_config *initialized;
	struct wl_display *display = c->backend->display;
	uint32_t enables = c->config_table->enable_globals;
	const char *shell_path;
	const char *console_path;

	initialized = tw_config_request_object(c, "initialized");
	shell_path =  tw_config_request_object(c, "shell_path");
	console_path = tw_config_request_object(c, "console_path");
	if (initialized)
		goto initialized;

	tw_config_register_object(c, "backend", c->backend);

	if (enables & TW_CONFIG_GLOBAL_BUS) {
		if (!(bus = tw_bus_create_global(c->backend->display)))
			goto out;
                tw_config_register_object(c, "bus", bus);
	}

	if (enables & TW_CONFIG_GLOBAL_TAIWINS_SHELL) {
		bool enable_layer_shell =
			enables & TW_CONFIG_GLOBAL_LAYER_SHELL;
		if (!(shell = tw_shell_create_global(display, c->backend,
		                                     enable_layer_shell,
		                                     shell_path)))
			goto out;
		tw_config_register_object(c, "shell", shell);
	}

	if (shell && (enables & TW_CONFIG_GLOBAL_TAIWINS_CONSOLE)) {
		if (!(console = tw_console_create_global(display,
		                                         console_path,
		                                         c->backend,
		                                         shell)))
			goto out;
		tw_config_register_object(c, "console", console);
	}

	if (enables & TW_CONFIG_GLOBAL_TAIWINS_THEME) {
		if (!(theme = tw_theme_create_global(display)))
			goto out;
		tw_config_register_object(c, "theme", theme);
	}
	/* if (!(xwayland = tw_setup_xwayland(ec))) */
	/*	goto out; */
	/* tw_config_register_object(c, "xwayland", xwayland); */
	if (enables & TW_CONFIG_GLOBAL_DESKTOP) {
		if (!(xdg = tw_xdg_create_global(display, shell, c->backend)))
			goto out;
		tw_config_register_object(c, "desktop", xdg);
	}
	//this is probably a bad idea.
	tw_config_register_object(c, "initialized", c->config_table);
initialized:
	tw_config_table_flush(c->config_table);
	return true;
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

static inline void
purge_xkb_rules(struct xkb_rule_names *rules)
{
	if (rules->rules)
		free((void *)rules->rules);
	rules->rules = NULL;
	if (rules->layout)
		free((void *)rules->layout);
	rules->layout = NULL;
	if (rules->model)
		free((void *)rules->model);
	rules->model = NULL;
	if (rules->options)
		free((void *)rules->options);
	rules->options = NULL;
	if (rules->variant)
		free((void *)rules->variant);
	rules->variant = NULL;
}

static inline void
complete_xkb_rules(struct xkb_rule_names *dst,
                   const struct xkb_rule_names *src)
{
	if (!dst->rules)
		dst->rules = src->rules;
	if (!dst->layout)
		dst->layout = src->layout;
	if (!dst->model)
		dst->model = src->model;
	if (!dst->options)
		dst->options = src->options;
	if (!dst->variant)
		dst->variant = src->variant;
}

static inline bool
xkb_rules_valid(struct xkb_rule_names *rules)
{
	return rules->rules || rules->layout ||
		rules->model || rules->options ||
		rules->variant;
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
	purge_xkb_rules(&table->xkb_rules);
	free(table);
}

void
tw_config_table_dirty(struct tw_config_table *t, bool dirty)
{
	struct wl_display *display;
	struct wl_event_loop *loop;

	if (t->config->_config_time || !dirty)
		return;
	display = t->config->backend->display;
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
	struct tw_xdg *desktop;
	struct tw_shell *shell;
	struct tw_xwayland *xwayland;
	struct tw_theme *theme;
	struct tw_backend *backend;

	desktop = tw_config_request_object(c, "desktop");
	xwayland = tw_config_request_object(c, "xwayland");
	theme = tw_config_request_object(c, "theme");
	backend = tw_config_request_object(c, "backend");
	shell = tw_config_request_object(c, "shell");

	for (int i = 0; backend && i < 32 ; i++) {
	}

	for (int i = 0; desktop && i < MAX_WORKSPACES; i++) {
		/* enum tw_layout_type layout = t->workspaces[i].layout.layout; */
		/* uint32_t igap = t->workspaces[i].desktop_igap.uval; */
		/* uint32_t ogap = t->workspaces[i].desktop_ogap.uval; */
		/* if (t->workspaces[i].layout.valid) */
		/*	tw_desktop_set_workspace_layout(desktop, i, layout); */
		/* t->workspaces[i].layout.valid = false; */
		/* if (t->workspaces[i].desktop_igap.valid) */
	}

	if (xwayland && t->xwayland.valid) {
		/* tw_xwayland_enable(xwayland, t->xwayland.enable); */
		/* t->xwayland.valid = false; */
	}

	//this is built on weston_compositor's middle layer approach, we can
	//create another object, not to cramble everything in the tw_backend.
	if (t->lock_timer.valid) {
		/* ec->idle_time = t->lock_timer.val; */
		/* t->lock_timer.valid = false; */
	}
	if (shell && t->panel_pos.valid) {
		tw_shell_set_panel_pos(shell, t->panel_pos.pos);
		t->panel_pos.valid = false;
	}

	if (theme && t->theme.valid) {
		/* tw_theme_notify(theme); */
		/* t->theme.read = false; */
		/* t->theme.valid = false; */
	}

	if (xkb_rules_valid(&t->xkb_rules)) {
		/* complete_xkb_rules(&t->xkb_rules, &ec->xkb_names); */
		/* weston_compositor_set_xkb_rule_names(ec, &t->xkb_rules); */
	}
	t->xkb_rules = (struct xkb_rule_names){0};

	if (t->kb_repeat.valid && t->kb_repeat.val > 0 &&
	    t->kb_delay.valid && t->kb_delay.val > 0) {
		/* tw_backend_set_repeat_info(c->backend, */
		/*                            t->kb_repeat.val, t->kb_delay.val); */
	}
}

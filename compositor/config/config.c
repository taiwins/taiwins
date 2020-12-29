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
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include <linux/input.h>
#include <ctype.h>
#include <ctypes/sequential.h>
#include <ctypes/os/file.h>
#include <ctypes/strops.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/utils.h>

#include <taiwins/engine.h>
#include <taiwins/output_device.h>
#include <taiwins/xdg.h>
#include <taiwins/shell.h>

#include "bindings.h"
#include "config.h"
#include "config_internal.h"

/******************************************************************************
 * internal helpers
 *****************************************************************************/

static const uint32_t TW_CONFIG_GLOBAL_DEFAULT =
	TW_CONFIG_GLOBAL_BUS |
	TW_CONFIG_GLOBAL_TAIWINS_SHELL |
	TW_CONFIG_GLOBAL_TAIWINS_CONSOLE |
	TW_CONFIG_GLOBAL_TAIWINS_THEME |
	TW_CONFIG_GLOBAL_LAYER_SHELL |
	TW_CONFIG_GLOBAL_XWAYLAND |
	TW_CONFIG_GLOBAL_DESKTOP;


static inline bool
copy_builtin_bindings(struct tw_binding *dst, const struct tw_binding *src)
{
	memcpy(dst, src, sizeof(struct tw_binding) * TW_BUILTIN_BINDING_SIZE);
	return true;
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
move_xkb_rules(struct xkb_rule_names *dst, struct xkb_rule_names *src)
{
	purge_xkb_rules(dst);
	*dst = *src;
	*src = (struct xkb_rule_names){0};
}

/******************************************************************************
 * config APIs
 *****************************************************************************/

const char *
tw_config_retrieve_error(struct tw_config *config)
{
	return config->err_msg;
}

static void
tw_config_copy_registry(struct tw_config *dst, struct tw_config *src)
{
	vector_copy(&dst->registry, &src->registry);
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

/**
 * @brief swap config like move assignment.
 *
 * at this point we know for sure we can apply the config. This works even if
 * our dst is a fresh new config. The release function will take care of freeing
 * things,
 */
static void
tw_swap_config(struct tw_config *dst, struct tw_config *src)
{
	//we are cloning user_data, which is luastate, cloning bindings,
	//config_table and all that.
	move_xkb_rules(&dst->xkb_rules, &src->xkb_rules);
	dst->config_table = src->config_table;

	//
	dst->fini(dst);
	dst->user_data = src->user_data;
	src->user_data = NULL;
	//
	vector_destroy(&dst->config_bindings);
	dst->config_bindings = src->config_bindings;
	vector_init_zero(&src->config_bindings, sizeof(struct tw_binding),
	                 NULL);
	copy_builtin_bindings(dst->builtin_bindings, src->builtin_bindings);
}

static bool
tw_try_config(struct tw_config *tmp_config, struct tw_config *main_config)
{
	char path[PATH_MAX];
	bool safe = true;

	tw_create_config_dir();
	tw_config_dir(path);
	strcat(path, "/config.lua");
	if (main_config->err_msg)
		free(main_config->err_msg);
	tmp_config->init(tmp_config);

	if (is_file_exist(path)) {
		safe = safe && tmp_config->read_config(tmp_config, path);
		if (!safe) {
			main_config->err_msg =
				tmp_config->read_error(tmp_config);
			tw_logl("config error: %s", main_config->err_msg);
		}
	}
	return safe;
}

static bool
tw_config_wake_compositor(struct tw_config *c)
{
	struct tw_shell *shell = NULL;
	struct tw_console *console;
	struct tw_bus *bus;
	struct tw_theme_global *theme;
	/* struct tw_xwayland *xwayland; */
	struct tw_xdg *xdg;
	struct tw_config *initialized;
	struct wl_display *display = c->engine->display;
	uint32_t enables = c->config_table.enable_globals;
	const char *shell_path;
	const char *console_path;

	initialized = tw_config_request_object(c, "initialized");
	shell_path =  tw_config_request_object(c, TW_CONFIG_SHELL_PATH);
	console_path = tw_config_request_object(c, TW_CONFIG_CONSOLE_PATH);
	if (initialized)
		goto initialized;

	/* tw_config_register_object(c, "backend", c->backend); */

	if (enables & TW_CONFIG_GLOBAL_BUS) {
		if (!(bus = tw_bus_create_global(display)))
			goto out;
                tw_config_register_object(c, "bus", bus);
	}

	if (enables & TW_CONFIG_GLOBAL_TAIWINS_SHELL) {
		bool enable_layer_shell =
			enables & TW_CONFIG_GLOBAL_LAYER_SHELL;
		if (!(shell = tw_shell_create_global(display, c->engine,
		                                     enable_layer_shell,
		                                     shell_path)))
			goto out;
		tw_config_register_object(c, "shell", shell);
	}

	if (shell && (enables & TW_CONFIG_GLOBAL_TAIWINS_CONSOLE)) {
		if (!(console = tw_console_create_global(display,
		                                         console_path,
		                                         c->engine,
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
		if (!(xdg = tw_xdg_create_global(display, shell, c->engine)))
			goto out;
		tw_config_register_object(c, "desktop", xdg);
	}
	//this is probably a bad idea.
	tw_config_register_object(c, "initialized", &c->config_table);
initialized:
	tw_config_table_flush(&c->config_table);
	return true;
out:
	return false;
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
	struct tw_bindings *tmp_bindings =
		tw_bindings_create(config->engine->display);

	temp_config = tw_config_create(config->engine, tmp_bindings,
	                               config->type);
	tw_config_copy_registry(temp_config, config);
	safe = tw_try_config(temp_config, config);

	if (safe) {
		//this is so tricky, would prefer a better way.
		tw_swap_config(config, temp_config);
		safe = safe && tw_config_install_bindings(config,
		                                          tmp_bindings);
		safe = safe && tw_config_wake_compositor(config);
		tw_bindings_move(config->bindings, tmp_bindings);
	}
	//in either case, we would want to purge the temp config
	tw_config_destroy(temp_config);
	tw_bindings_destroy(tmp_bindings);

	return safe;
}

bool
tw_run_default_config(struct tw_config *c)
{
	bool safe = true;
	c->config_table.enable_globals = TW_CONFIG_GLOBAL_DEFAULT;
	safe = tw_config_wake_compositor(c);
	safe = safe && tw_config_install_bindings(c, c->bindings);
	return safe;
}

static inline struct tw_config_output *
tw_config_output_from_backend_output(struct tw_config_table *t,
                                     struct tw_engine_output *output)
{
	for (unsigned i = 0; i < NUMOF(t->outputs); i++) {
		if (!strcmp(output->name, t->outputs[i].name))
			return &t->outputs[i];
	}
	return NULL;
}

static void
tw_config_apply_output(struct tw_config_table *t,
                       struct tw_engine_output *bo)
{
	struct tw_output_device *od = bo->device;
	struct tw_config_output *co =
		tw_config_output_from_backend_output(t, bo);

	if (!co || !od)
		return;
	if (co->enabled.valid)
		tw_output_device_enable(od, co->enabled.enable);
	if (co->transform.valid)
		tw_output_device_set_transform(od, co->transform.transform);
	if (co->width.valid && co->height.valid)
		tw_output_device_set_custom_mode(od, co->width.uval,
		                                 co->height.uval, 0);
	if (co->posx.valid && co->posy.valid)
		tw_output_device_set_pos(od, co->posx.val, co->posy.val);
	if (co->scale.valid)
		tw_output_device_set_scale(od, co->scale.val);
	tw_output_device_commit_state(od);
}

static void
notify_config_output_create(struct wl_listener *listener, void *data)
{
	struct tw_config *config =
		container_of(listener, struct tw_config,
		             output_created_listener);
	tw_config_apply_output(&config->config_table, data);
}

static void
notify_config_seat_create(struct wl_listener *listener, void *data)
{
	struct tw_config *config =
		wl_container_of(listener, config, seat_created_listener);
	struct tw_engine_seat *seat = data;

	tw_engine_seat_set_xkb_rules(seat, &config->xkb_rules);
}

struct tw_config*
tw_config_create(struct tw_engine *engine, struct tw_bindings *bindings,
                 enum tw_config_type type)
{
	assert(type == TW_CONFIG_TYPE_LUA);

	struct tw_config *config =
		calloc(1, sizeof(struct tw_config));
	if (!config)
		return NULL;

	config->type = type;
	config->engine = engine;
	config->err_msg = NULL;
	config->user_data = NULL;
	config->bindings = bindings;
	config->config_table.enable_globals = TW_CONFIG_GLOBAL_DEFAULT;
	config->config_table.dirty = false;
	config->config_table.xkb_rules = &config->xkb_rules;

	config->init = tw_luaconfig_init;
	config->fini = tw_luaconfig_fini;
	config->read_error = tw_luaconfig_read_error;
	config->read_config = tw_luaconfig_read;

	tw_config_default_bindings(config);

	vector_init_zero(&config->config_bindings,
	                 sizeof(struct tw_binding), NULL);
	vector_init_zero(&config->registry,
	                 sizeof(struct tw_config_obj), NULL);
	tw_signal_setup_listener(&engine->signals.output_created,
	                         &config->output_created_listener,
	                         notify_config_output_create);
	tw_signal_setup_listener(&engine->signals.seat_created,
	                         &config->seat_created_listener,
	                         notify_config_seat_create);
	return config;
}

void
tw_config_destroy(struct tw_config *config)
{
	if (config->err_msg)
		free(config->err_msg);
	config->fini(config);
	config->bindings = NULL;
	purge_xkb_rules(&config->xkb_rules);
	vector_destroy(&config->config_bindings);
	vector_destroy(&config->registry);

	wl_list_remove(&config->output_created_listener.link);
        wl_list_remove(&config->seat_created_listener.link);
        free(config);
}

void
tw_config_table_dirty(struct tw_config_table *t, bool dirty)
{
	if (t->dirty || !dirty)
		return;
	t->dirty = true;

}

/* this function is the only point we apply for configurations, It can may run
 * in the middle of the configuration as well. For example, if lua config is
 * calling compositor.wake(). tw_config_table_apply would run and apply for the
 * configuration first before actually wakening the comositor.
*/
void
tw_config_table_flush(struct tw_config_table *t)
{
	struct tw_xdg *desktop;
	struct tw_shell *shell;
	struct tw_theme_global *theme;
	struct tw_engine *engine;
	struct tw_engine_output *output;
	struct tw_engine_seat *seat;
	struct tw_config *c = wl_container_of(t, c, config_table);

	engine = c->engine;
	desktop = tw_config_request_object(c, "desktop");
	theme = tw_config_request_object(c, "theme");
	shell = tw_config_request_object(c, "shell");
	if (!t->dirty)
		return;

	wl_list_for_each(output, &engine->heads, link)
		tw_config_apply_output(t, output);

	wl_list_for_each(seat, &engine->inputs, link)
		tw_engine_seat_set_xkb_rules(seat, t->xkb_rules);

	for (unsigned i = 0; desktop && i < MAX_WORKSPACES; i++) {
		tw_xdg_set_workspace_layout(desktop, i,
		                            t->workspaces[i].layout.layout);
	}
	if (desktop && t->desktop_igap.valid && t->desktop_ogap.valid) {
		tw_xdg_set_desktop_gap(desktop, t->desktop_igap.uval,
		                       t->desktop_ogap.uval);
		t->desktop_igap.valid = false;
		t->desktop_ogap.valid = false;
	}

	if (t->lock_timer.valid) {
	}

	if (shell && t->panel_pos.valid) {
		tw_shell_set_panel_pos(shell, t->panel_pos.pos);
		t->panel_pos.valid = false;
	}

	if (theme && t->theme.valid) {
		tw_theme_notify(theme, t->theme.theme);
		t->theme.theme = NULL;
		t->theme.valid = false;
	}

	if (t->kb_repeat.valid && t->kb_repeat.val > 0 &&
	    t->kb_delay.valid && t->kb_delay.val > 0) {
		//TODO: set repeat info.
	}
}

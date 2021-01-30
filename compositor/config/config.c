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
#include <sys/types.h>
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
#include <taiwins/objects/logger.h>

#include <taiwins/engine.h>
#include <taiwins/output_device.h>
#include <taiwins/shell.h>

#include "xdg.h"
#include "bindings.h"
#include "config.h"

struct tw_config_obj {
	char name[32];
	void *data;
};

#define SAFE_FREE(ptr) \
	do { \
		if ((ptr)) { \
			free(((void *)ptr)); \
			ptr = NULL; \
		} \
	} while (0)

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

static inline void
purge_xkb_rules(struct xkb_rule_names *rules)
{
	SAFE_FREE(rules->rules);
	SAFE_FREE(rules->layout);
	SAFE_FREE(rules->model);
	SAFE_FREE(rules->options);
	SAFE_FREE(rules->variant);
}

/******************************************************************************
 * registry
 *****************************************************************************/
void
tw_config_register_object(struct tw_config *config,
                          const char *name, void *obj)
{
	struct tw_config_obj object;
	struct tw_config_obj *find;

	vector_for_each(find, &config->current->registry) {
		if (!strcmp(find->name, name)) {
			find->data = obj;
			return;
		}
	}

	strop_ncpy((char *)object.name, name, 32);
	object.data = obj;
	vector_append(&config->current->registry, &object);
}

void *
tw_config_request_object(struct tw_config *config,
                         const char *name)
{
	struct tw_config_obj *obj;

	vector_for_each(obj, &config->current->registry) {
		if (!strcmp(name, obj->name))
			return obj->data;
	}
	return NULL;
}

/******************************************************************************
 * config table
 *****************************************************************************/

//the structure before was easier, because we create brand new config
//everytime. Right now we are arguing when do we invoke it,
static void
tw_config_table_init(struct tw_config_table *table, struct tw_config *config,
                     vector_t *registry)
{
	//do we init lua script here?
	tw_bindings_init(&table->bindings, config->engine->display);
	table->config = config;
	table->enable_globals = TW_CONFIG_GLOBAL_DEFAULT;
	table->dirty = false;
	table->user_data = NULL;
	tw_config_default_bindings(table->builtin_bindings);
	vector_init_zero(&table->config_bindings, sizeof(struct tw_binding),
	                 NULL);
	vector_init_zero(&table->registry, sizeof(struct tw_config_obj), NULL);
	if (registry)
		vector_copy(&table->registry, registry);
	//TODO init xkb_rules, the rules right now is shared to the
	//engine_seat
}

static void
tw_config_table_fini(struct tw_config_table *table)
{
	struct tw_config *config = table->config;

	tw_bindings_release(&table->bindings);
	table->dirty = false;
	vector_destroy(&table->registry);
	vector_destroy(&table->config_bindings);
	if (table->user_data) {
		config->fini(table);
		table->user_data = NULL;
	}
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
tw_config_table_apply_output(struct tw_config_table *t,
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

/* this function is the only point we apply for configurations, It can may run
 * in the middle of the configuration as well. For example, if lua config is
 * calling compositor.wake(). tw_config_table_apply would run and apply for the
 * configuration first before actually wakening the comositor.
*/
static void
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
		tw_config_table_apply_output(t, output);

	wl_list_for_each(seat, &engine->inputs, link)
		tw_engine_seat_set_xkb_rules(seat, &t->xkb_rules);

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

/******************************************************************************
 * config runners
 *****************************************************************************/

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
	return true;
out:
	return false;
}

/**
 * @brief swap config like move assignment.
 *
 * at this point we know for sure we can apply the config. This works even if
 * our dst is a fresh new config. The release function will take care of
 * freeing things,
 */
static void
tw_config_apply_table(struct tw_config *config, struct tw_config_table *src)
{
	struct tw_config_table *dst = &config->config_table;

	//purge
	purge_xkb_rules(&dst->xkb_rules);
	vector_destroy(&dst->registry);
	vector_destroy(&dst->config_bindings);
	tw_bindings_release(&dst->bindings);
	config->fini(dst);
	dst->user_data = NULL;
	//a shallow copy
	memcpy(dst, src, sizeof(*dst));
	//the data requires deep copy
	tw_bindings_copy(&dst->bindings, &src->bindings);
}

static bool
tw_try_config(struct tw_config_table *pending, char **err_msg)
{
	char path[PATH_MAX];
	bool safe = true;
	struct tw_config *config = pending->config;

	tw_create_config_dir();
	tw_config_dir(path);
	strcat(path, "/config.lua");

	pending->config->init(pending);

	if (is_file_exist(path)) {
		*err_msg = config->run(pending, path);
		safe = safe && !(*err_msg);
	}

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
tw_config_run(struct tw_config *config, char **err_msg)
{
	bool safe;
	struct tw_config_table pending = {0};

	tw_config_table_init(&pending, config, &config->config_table.registry);
	//we now use temporary config table
	config->current = &pending;
	safe = tw_try_config(&pending, err_msg);
	safe = safe && tw_config_install_bindings(&pending);
	safe = safe && tw_config_wake_compositor(config);

	if (safe) {
		tw_config_apply_table(config, &pending);
		tw_config_table_flush(&config->config_table);
	} else {
		tw_config_table_fini(&pending);
	}
	//config table now points back
	config->current = &config->config_table;
	return safe;
}

bool
tw_config_run_default(struct tw_config *config)
{
	bool safe = true;
	config->config_table.enable_globals = TW_CONFIG_GLOBAL_DEFAULT;
	safe = safe && tw_config_install_bindings(&config->config_table);
	safe = tw_config_wake_compositor(config);
	tw_config_table_flush(&config->config_table);
	return safe;
}

static void
notify_config_output_create(struct wl_listener *listener, void *data)
{
	struct tw_config *config =
		container_of(listener, struct tw_config,
		             output_created_listener);
	tw_config_table_apply_output(&config->config_table, data);
}

static void
notify_config_seat_create(struct wl_listener *listener, void *data)
{
	struct tw_config *config =
		wl_container_of(listener, config, seat_created_listener);
	struct tw_engine_seat *seat = data;

	tw_engine_seat_set_xkb_rules(seat, &config->config_table.xkb_rules);
}

//this should be renamed as tw_config_lua
void
tw_config_init(struct tw_config *config, struct tw_engine *engine)
{
	//to change
	config->engine = engine;
	tw_config_table_init(&config->config_table, config, NULL);
	config->current = &config->config_table;
	tw_signal_setup_listener(&engine->signals.output_created,
	                         &config->output_created_listener,
	                         notify_config_output_create);
	tw_signal_setup_listener(&engine->signals.seat_created,
	                         &config->seat_created_listener,
	                         notify_config_seat_create);

}

void
tw_config_fini(struct tw_config *config)
{
	tw_config_table_fini(&config->config_table);
	wl_list_remove(&config->output_created_listener.link);
	wl_list_remove(&config->seat_created_listener.link);
}

void
tw_config_table_dirty(struct tw_config_table *t, bool dirty)
{
	if (t->dirty || !dirty)
		return;
	t->dirty = true;
}

/*
 * config.h - taiwins config shared header
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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <shared_config.h>
#include <ctypes/vector.h>
#include <taiwins/engine.h>

#include "bindings.h"
#include "desktop/xdg.h"
#include "config_types.h"
#include "config_bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

//TODO: using enum instead of names
#define TW_CONFIG_SHELL_PATH "shell_path"
#define TW_CONFIG_CONSOLE_PATH "console_path"

enum tw_config_type {
	TW_CONFIG_TYPE_LUA,
	TW_CONFIG_TYPE_DBUS,
};

/**
 * options to enable objects, objects like backend is completely necessary,
 * thus not listed here.
 *
 * Or I can enable them on the fly, this could be unplesant though.
 */
enum tw_config_enable_global {
	TW_CONFIG_GLOBAL_BUS = (1 << 0),
	TW_CONFIG_GLOBAL_TAIWINS_SHELL = (1 << 1),
	TW_CONFIG_GLOBAL_TAIWINS_CONSOLE = (1 << 2),
	TW_CONFIG_GLOBAL_TAIWINS_THEME = (1 << 3),
	TW_CONFIG_GLOBAL_LAYER_SHELL = (1 << 4),
	TW_CONFIG_GLOBAL_XWAYLAND = (1 << 5),
	TW_CONFIG_GLOBAL_DESKTOP = (1 << 6),
};

struct tw_config_table {
	bool dirty;
	uint32_t enable_globals;
	struct tw_config_output outputs[8];

	struct {
		pending_layout_t layout;
	} workspaces[MAX_WORKSPACES];

        pending_uintval_t desktop_igap;
	pending_uintval_t desktop_ogap;
	pending_panel_pos_t panel_pos;
	pending_intval_t sleep_timer;
	pending_intval_t lock_timer;
	pending_theme_t theme;

	pending_intval_t kb_repeat; /**< invalid: -1 */
	pending_intval_t kb_delay; /**< invalid: -1 */

	//TODO New data here, what we archive? One config
	struct xkb_rule_names xkb_rules;
	vector_t registry;
	vector_t config_bindings;
	struct tw_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];
	struct tw_bindings bindings;
	struct tw_config *config;
	void *user_data; //lua state
};

/**
 * @brief the taiwins config object
 *
 * For now, it is designed as runing config through a script, the config is
 * replacable, I think we should make the part of it replacable, or simply make
 *
 */
struct tw_config {
	struct tw_engine *engine;

	enum tw_config_type type;
        /* current mostly points to config_table excpet when we runs a config
         * file, it points to a temporary config_table it would be moved back
         * later
         */
	struct tw_config_table config_table, *current;


	/**< lua code may use this */
	struct wl_listener output_created_listener;
	struct wl_listener seat_created_listener;

	//ideally, we would use function pointers to wrap lua code together
	void (*init)(struct tw_config_table *);
	void (*fini)(struct tw_config_table *);
	char *(*run)(struct tw_config_table *, const char *);
};

void
tw_config_init_lua(struct tw_config *c, struct tw_engine *engine);

void
tw_config_fini(struct tw_config *c);

bool
tw_config_run(struct tw_config *config, char **err_msg);

bool
tw_config_run_default(struct tw_config *c);

void
tw_config_destroy(struct tw_config *config);

void
tw_config_register_object(struct tw_config *config,
                          const char *name, void *obj);
void *
tw_config_request_object(struct tw_config *config,
                         const char *name);

/* the bus would be used for configuration anyway, we probably just move it
 * inside config
 */
struct tw_bus *
tw_bus_create_global(struct wl_display *display);

/******************************************************************************
 * private APIs
 *****************************************************************************/

void
tw_config_table_dirty(struct tw_config_table *table, bool dirty);



#ifdef __cplusplus
}
#endif


#endif /* EOF */

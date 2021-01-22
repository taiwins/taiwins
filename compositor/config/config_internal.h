/*
 * config_internal.h - taiwins config shared header
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

#ifndef CONFIG_INTERNAL_H
#define CONFIG_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wayland-taiwins-shell-server-protocol.h>
#include <shared_config.h>
#include <ctypes/vector.h>
#include <taiwins/objects/logger.h>

#include <taiwins/engine.h>
#include "xdg.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tw_builtin_binding_t {
	TW_QUIT_BINDING = 0,
	TW_CLOSE_APP_BINDING,
	TW_RELOAD_CONFIG_BINDING,
	//QUIT taiwins, rerun configuration
	//console
	TW_OPEN_CONSOLE_BINDING,
	//shell
	TW_ZOOM_AXIS_BINDING,
	TW_ALPHA_AXIS_BINDING,
	//views
	TW_MOVE_PRESS_BINDING,
	TW_FOCUS_PRESS_BINDING,
	TW_RESIZE_ON_LEFT_BINDING,
	TW_RESIZE_ON_RIGHT_BINDING,
	TW_RESIZE_ON_UP_BINDING,
	TW_RESIZE_ON_DOWN_BINDING,
	//workspace
	TW_SWITCH_WS_LEFT_BINDING,
	TW_SWITCH_WS_RIGHT_BINDING,
	TW_SWITCH_WS_RECENT_BINDING,
	TW_TOGGLE_FLOATING_BINDING,
	TW_TOGGLE_VERTICAL_BINDING,
	TW_VSPLIT_WS_BINDING,
	TW_HSPLIT_WS_BINDING,
	TW_MERGE_BINDING,
	//view cycling
	TW_NEXT_VIEW_BINDING,
	//sizeof
	TW_BUILTIN_BINDING_SIZE
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

struct tw_config_obj {
	char name[32];
	void *data;
};

typedef struct {
	int rotate;
	bool flip;
	enum wl_output_transform t;
} tw_config_transform_t;

typedef OPTION(enum wl_output_transform, transform) pending_transform_t;
typedef OPTION(enum tw_layout_type, layout) pending_layout_t;
typedef OPTION(bool, enable) pending_boolean_t;
typedef OPTION(enum taiwins_shell_task_switch_effect, eff) pending_effect_t;
typedef OPTION(enum taiwins_shell_panel_pos, pos) pending_panel_pos_t;
typedef OPTION(int32_t, val) pending_intval_t;
typedef OPTION(uint32_t, uval) pending_uintval_t;
typedef OPTION(struct tw_theme *, theme) pending_theme_t;

#define SET_PENDING(ptr, name, value)                                   \
	({ \
		(ptr)->name = value; \
		(ptr)->valid = true; \
	})

struct tw_config_output {
	char name[24];
	pending_intval_t scale, posx, posy;
	pending_uintval_t width, height;
	pending_transform_t transform;
	pending_boolean_t enabled;
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

	struct xkb_rule_names *xkb_rules;
	pending_intval_t kb_repeat; /**< invalid: -1 */
	pending_intval_t kb_delay; /**< invalid: -1 */
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
	struct tw_bindings *bindings;
	enum tw_config_type type;
	//this is stupid, we can simply embed the struct in
	struct tw_config_table config_table;
	vector_t registry;

	vector_t config_bindings;
	struct tw_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];
	struct xkb_rule_names xkb_rules;

	/**< lua code may use this */
	struct wl_listener output_created_listener;
	struct wl_listener seat_created_listener;

	//ideally, we would use function pointers to wrap lua code together
	void (*init)(struct tw_config *);
	void (*fini)(struct tw_config *);
	bool (*read_config)(struct tw_config *, const char *);
	char *(*read_error)(struct tw_config *);
	void *user_data;
	char *err_msg;
};

void
tw_config_default_bindings(struct tw_config *c);

bool
tw_config_install_bindings(struct tw_config *c, struct tw_bindings *root);

bool
parse_one_press(const char *str, const enum tw_binding_type type,
                uint32_t *mod, uint32_t *code);
const char *
tw_config_retrieve_error(struct tw_config *);

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

void
tw_config_table_flush(struct tw_config_table *table);

extern bool
tw_luaconfig_read(struct tw_config *c, const char *path);

extern char *
tw_luaconfig_read_error(struct tw_config *c);

void
tw_luaconfig_fini(struct tw_config *c);

void
tw_luaconfig_init(struct tw_config *c);

#ifdef __cplusplus
}
#endif


#endif /* EOF */

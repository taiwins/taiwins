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

#include <libweston/libweston.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <shared_config.h>

#include "../config.h"
#include "server/taiwins.h"


#ifdef __cplusplus
extern "C" {
#endif

struct tw_config {
	char path[128];	/**< it is copied on first time runing config */
	struct weston_compositor *compositor;
	struct tw_bindings *bindings;
	lua_State *L;
	log_func_t print;
	char *err_msg;
	bool quit;
	bool _config_time; /**< mark for configuration is running */
	const char *shell_path, *console_path;
	vector_t registry;

	/**< lua code may use this */
	struct wl_listener output_created_listener;
	struct wl_listener output_destroyed_listner;

	struct wl_list lua_components;
	struct wl_list apply_bindings;
	vector_t option_hooks;

	/**< compositor option caches, and invalid values */
	struct {
		struct xkb_rule_names xkb_rules;
		int32_t kb_repeat; /**< invalid: -1 */
		int32_t kb_delay; /**< invalid: -1 */
	};
	/**< user bindings */
	vector_t config_bindings;
	/**< builtin bindings */
	struct tw_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];
};

void
tw_config_init_luastate(struct tw_config *c);

bool
parse_one_press(const char *str, const enum tw_binding_type type,
                uint32_t *mod, uint32_t *code);

/* as for now we will try to make config more of a compositor thing by spliting
 * everything from the config.
 *
 * We want a c config API. This end config is used by lua config, essentially
 * moving stuff from taiwins.h to here.
 *
 * some of those apis does not need to exist, for example, you can simply use
 * `weston_output_*` for output manipulations.
 */
void
tw_load_default_config(struct tw_config *c);

/*******************************************************************************
 * private APIs
 ******************************************************************************/
struct tw_config_obj {
	char name[32];
	void *data;
};

enum tw_config_ws_layout {
	TW_FLOATING_LAYOUT,
	TW_TILING_LAYOUT,
};


typedef struct {
	int rotate;
	bool flip;
        enum wl_output_transform t;
} tw_config_transform_t;

typedef OPTION(enum wl_output_transform, transform) pending_transform_t;
typedef OPTION(enum tw_config_ws_layout, layout) pending_layout_t;
typedef OPTION(bool, enable) pending_xwayland_enable_t;
typedef OPTION(char *, path) pending_path_t;
typedef OPTION(enum taiwins_shell_panel_pos, pos) pending_panel_pos_t;
typedef OPTION(enum taiwins_shell_task_switch_effect, eff) pending_effect_t;
typedef OPTION(vector_t, vec) pending_vec_t;
typedef OPTION(int32_t, val) pending_intval_t;
typedef OPTION(bool, read) pending_theme_reading_t;


#define SET_PENDING(ptr, name, value)                                   \
	({ \
		(ptr)->name = value; \
		(ptr)->valid = true; \
	})

struct tw_config_table {
	struct {
		struct weston_output *output;
		pending_intval_t scale;
		pending_transform_t transform;
	} outputs[32];

	struct {
		pending_layout_t layout;
	} workspaces[10];

	pending_intval_t desktop_igap;
	pending_intval_t desktop_ogap;
	pending_xwayland_enable_t xwayland;
	pending_theme_reading_t theme;

        pending_path_t background_path;
	pending_path_t widgets_path;
	pending_panel_pos_t panel_pos;
	pending_vec_t menu;
	pending_intval_t sleep_timer;
	pending_intval_t lock_timer;

	struct xkb_rule_names xkb_rules;
	int32_t kb_repeat; /**< invalid: -1 */
	int32_t kb_delay; /**< invalid: -1 */

	struct tw_config *config;
};

void
tw_config_table_dirty(struct tw_config_table *table, bool dirty);

void
tw_config_table_flush(struct tw_config_table *table);

void *
tw_config_request_object(struct tw_config *config,
                         const char *name);


#ifdef __cplusplus
}
#endif


#endif /* EOF */

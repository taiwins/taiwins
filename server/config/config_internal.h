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
#include <libweston/libweston.h>
#include <wayland-server-protocol.h>
#include <shared_config.h>

#include "../compositor.h"
#include "server/taiwins.h"
#include "vector.h"


#ifdef __cplusplus
extern "C" {
#endif

enum tw_builtin_binding_t {
	TW_QUIT_BINDING,
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
	//workspace
	TW_SWITCH_WS_LEFT_BINDING,
	TW_SWITCH_WS_RIGHT_BINDING,
	TW_SWITCH_WS_RECENT_BINDING,
	TW_TOGGLE_FLOATING_BINDING,
	TW_TOGGLE_VERTICAL_BINDING,
	TW_VSPLIT_WS_BINDING,
	TW_HSPLIT_WS_BINDING,
	TW_MERGE_BINDING,
	//resize
	TW_RESIZE_ON_LEFT_BINDING,
	TW_RESIZE_ON_RIGHT_BINDING,
	//view cycling
	TW_NEXT_VIEW_BINDING,
	//sizeof
	TW_BUILTIN_BINDING_SIZE
};

struct tw_config_table;

struct tw_config {
	struct weston_compositor *compositor;
	struct tw_bindings *bindings;
	struct tw_config_table *config_table;
	log_func_t print;
	bool quit;
	bool _config_time; /**< mark for configuration is running */
	vector_t registry;
	vector_t config_bindings;
	struct tw_binding builtin_bindings[TW_BUILTIN_BINDING_SIZE];

	/**< lua code may use this */
	struct wl_listener output_created_listener;
	struct wl_listener output_destroyed_listener;

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

/**
 * @brief get the configuration for keybinding
 */
const struct tw_binding *
tw_config_get_builtin_binding(struct tw_config *, enum tw_builtin_binding_t);

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

bool
tw_config_wake_compositor(struct tw_config *c);
/*******************************************************************************
 * private APIs
 ******************************************************************************/

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

static inline void
SET_PENDING_STR(pending_path_t *path, char *value)
{
	if (path->path)
		free(path);
	path->path = value;
	path->valid = true;
}

static inline void
SET_PENDING_VEC(pending_vec_t *vec, vector_t *copy)
{
	if (vec->vec.elems)
		vector_destroy(&vec->vec);
	vec->vec = *copy;
	vec->valid = true;
}

struct tw_config_table {
	struct {
		struct weston_output *output;
		pending_intval_t scale;
		pending_transform_t transform;
	} outputs[32];

	struct {
		pending_layout_t layout;
	} workspaces[MAX_WORKSPACE];

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
	pending_intval_t kb_repeat; /**< invalid: -1 */
	pending_intval_t kb_delay; /**< invalid: -1 */

	struct tw_config *config;
};

struct tw_config_table *
tw_config_table_new(struct tw_config *c);

void
tw_config_table_destroy(struct tw_config_table *);

void
tw_config_table_dirty(struct tw_config_table *table, bool dirty);

void
tw_config_table_flush(struct tw_config_table *table);


#ifdef __cplusplus
}
#endif


#endif /* EOF */

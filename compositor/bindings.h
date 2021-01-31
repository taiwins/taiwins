/*
 * bindings.h - taiwins bindings header
 *
 * Copyright (c) 2020 Xichen Zhou
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

#ifndef TW_BINDINGS_H
#define TW_BINDINGS_H

#include <stdbool.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <taiwins/objects/seat.h>
#include <ctypes/tree.h>
#include <ctypes/vector.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef MAX_KEY_SEQ_LEN
#define MAX_KEY_SEQ_LEN 5
#endif

struct tw_bindings;
struct tw_binding_keystate;
struct tw_binding_node;

enum tw_binding_type {
	TW_BINDING_INVALID,
	TW_BINDING_key,
	TW_BINDING_btn,
	TW_BINDING_axis,
	TW_BINDING_tch,
};

struct tw_key_press {
	uint32_t keycode;
	uint32_t modifier;
};

struct tw_btn_press {
	uint32_t btn;
	uint32_t modifier;
};

struct tw_axis_motion {
	enum wl_pointer_axis axis_event;
	uint32_t modifier;
};

struct tw_touch_action {
	//we could have touch_id here, to handle gesture
	//for now, we deal with only modifier
	uint32_t modifier;
};

/*
 * return true in for the signals we want to block. Maybe in the future we
 * would have a chain of grab.
 */
typedef bool (*tw_key_binding)(struct tw_keyboard *keyboard, uint32_t time,
                               uint32_t key, uint32_t mods, uint32_t option,
                               void *data);
typedef bool (*tw_btn_binding)(struct tw_pointer *pointer, uint32_t time_msec,
                               uint32_t btn, uint32_t mods, void *data);
typedef bool (*tw_axis_binding)(struct tw_pointer *pointer, uint32_t time,
                                double delta, enum wl_pointer_axis direction,
                                uint32_t mods, void *data);
typedef bool (*tw_touch_binding)(struct tw_touch *touch, uint32_t time,
                                 uint32_t mods, void *data);

struct tw_binding {
	char name[32];
	enum tw_binding_type type;
	union {
		struct tw_key_press keypress[MAX_KEY_SEQ_LEN];
		struct tw_btn_press btnpress;
		struct tw_axis_motion axisaction;
		struct tw_touch_action touch;
	};
	union {
		tw_btn_binding btn_func;
		tw_axis_binding axis_func;
		tw_touch_binding touch_func;
		tw_key_binding key_func;
	};
	//for user_binding, this is a lua state, for builtin bindings, it is the
	//passing in user data
	void *user_data;
	uint32_t option;
};

struct tw_binding_node {
	struct vtree_node node;
	uint32_t keycode;
	uint32_t modifier;
	//this is a private option you need to have for
	bool end;
	struct tw_binding binding;
};

struct tw_bindings {
	//root node for keyboard
	struct wl_display *display;
	struct tw_binding_node root_node;
	struct wl_listener destroy_listener;
	vector_t apply_list;
};

/**
 * tw_bindings is used in configurations, used by configuration. User would swap
 * to new binding system in run-time.
 */
struct tw_bindings *
tw_bindings_create(struct wl_display *);

void
tw_bindings_init(struct tw_bindings *root, struct wl_display *display);

void
tw_bindings_release(struct tw_bindings *bindings);

void
tw_bindings_destroy(struct tw_bindings *);

/**
 * @brief ability to move assign src bindings to dst, so we can avoid deleting
 * all the bindings.
 */
void
tw_bindings_move(struct tw_bindings *dst, struct tw_bindings *src);

void
tw_bindings_copy(struct tw_bindings *dst, struct tw_bindings *src);

bool
tw_bindings_add_key(struct tw_bindings *root,
                    const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
                    const tw_key_binding binding, uint32_t option,
                    void *data);
bool
tw_bindings_add_btn(struct tw_bindings *root,
                    const struct tw_btn_press *press,
                    const tw_btn_binding binding,
                    void *data);
bool
tw_bindings_add_axis(struct tw_bindings *root,
                     const struct tw_axis_motion *motion,
                     const tw_axis_binding binding,
                     void *data);
bool
tw_bindings_add_touch(struct tw_bindings *root,
                      uint32_t modifier, const tw_touch_binding binding,
                      void *data);
struct tw_binding_node *
tw_bindings_find_key(struct tw_bindings *bindings,
                     uint32_t key, uint32_t mod_mask);
struct tw_binding *
tw_bindings_find_btn(struct tw_bindings *bindings, uint32_t btn,
                     uint32_t mod_mask);
struct tw_binding *
tw_bindings_find_axis(struct tw_bindings *bindings,
                      enum wl_pointer_axis action, uint32_t mod_mask);

struct tw_binding *
tw_bindings_find_touch(struct tw_bindings *bindings, uint32_t mod_mask);

struct tw_binding *
tw_binding_node_get_binding(struct tw_binding_node *state);

/**
 * @brief search sub keystate in the keystate
 */
struct tw_binding_node *
tw_binding_node_step(struct tw_binding_node *keystate,
                     uint32_t keycode, uint32_t mod_mask);

void
tw_bindings_print(struct tw_bindings *root);

#ifdef  __cplusplus
}
#endif


#endif

/*
 * bindings.h - taiwins bindings header
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

#ifndef TW_BINDINGS_H
#define TW_BINDINGS_H

#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "taiwins.h"

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef MAX_KEY_SEQ_LEN
#define MAX_KEY_SEQ_LEN 5
#endif


/*
 * we are creating an additional interface on top of libweston binding system to
 * overcome some of its limitations(e.g, weston binding is limited to one press,
 * to implement emacs like keypress sequence, we need to take advantage of the
 * weston grab system).
 *
 */

/*
 * /brief key press types
 */
struct tw_key_press {
	uint32_t keycode;
	uint32_t modifier;
};

/*
 * /brief key press types
 */
struct tw_btn_press {
	uint32_t btn;
	uint32_t modifier;
};

/*
 * /brief key press types
 */
struct tw_axis_motion {
	enum wl_pointer_axis axis_event;
	uint32_t modifier;
};

/*
 * /brief list of possible inputs
 *
 * those are the possible inputs provides defaultly by libweston
 */
enum tw_binding_type {
	TW_BINDING_INVALID,
	TW_BINDING_key,
	TW_BINDING_btn,
	TW_BINDING_axis,
	TW_BINDING_tch,
};



typedef weston_button_binding_handler_t tw_btn_binding;
typedef weston_axis_binding_handler_t tw_axis_binding;
typedef weston_touch_binding_handler_t tw_touch_binding;
/* we provide additional extensions to keyboard bindings */
typedef void (*tw_key_binding)(struct weston_keyboard *keyboard,
			       const struct timespec *time, uint32_t key,
			       uint32_t option, void *data);

/**
 * /brief tw_bindings
 */
struct tw_bindings;

//the name of this struct is curious
struct taiwins_binding {
	char name[32];
	enum tw_binding_type type;
	union {
		struct tw_key_press keypress[MAX_KEY_SEQ_LEN];
		struct tw_btn_press btnpress;
		struct tw_axis_motion axisaction;
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
};


struct tw_bindings *tw_bindings_create(struct weston_compositor *);
void tw_bindings_destroy(struct tw_bindings *);

bool tw_bindings_add_key(struct tw_bindings *root,
			 const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
			 const tw_key_binding binding, uint32_t option,
			 void *data);

//it is normally implemented like this, unless you have grabs,
bool tw_bindings_add_btn(struct tw_bindings *root,
			 const struct tw_btn_press *press,
			 const tw_btn_binding binding,
			 void *data);

bool tw_bindings_add_axis(struct tw_bindings *root,
			  const struct tw_axis_motion *motion,
			  const tw_axis_binding binding,
			  void *data);

bool tw_bindings_add_touch(struct tw_bindings *root,
			   enum weston_keyboard_modifier modifier,
			   const tw_touch_binding binding,
			   void *data);

void tw_bindings_print(struct tw_bindings *root);

void tw_bindings_apply(struct tw_bindings *root);



/* bool tw_parse_binding(const char *code_str, const enum tw_binding_type type, */
/*		      struct tw_press *press); */


#ifdef  __cplusplus
}
#endif


#endif

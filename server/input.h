#ifndef TW_INPUT_H
#define TW_INPUT_H

#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <compositor.h>
#include <tree.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef MAX_KEY_SEQ_LEN
#define MAX_KEY_SEQ_LEN 5
#endif


struct tw_key_press {
	xkb_keycode_t keycode;
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


//define it as input agnostic handler
enum tw_binding_type {
	TW_BINDING_key,
	TW_BINDING_btn,
	TW_BINDING_axis,
	TW_BINDING_tch,
};

struct tw_press {
	union {
		xkb_keycode_t keycode;
		uint32_t btn;
		enum wl_pointer_axis axis;
		bool tch;
	};
	uint32_t modifier;
};

typedef void (*tw_key_binding)(struct weston_keyboard *keyboard, uint32_t option, void *data);
typedef void (*tw_btn_binding)(struct weston_pointer *pointer, uint32_t option, void *data);
typedef void (*tw_axis_binding)(struct weston_pointer *pointer, uint32_t option,
				struct weston_pointer_axis_event *event, void *data);

struct tw_binding_node {
	union {
		xkb_keycode_t keycode;
		uint32_t btn;
		enum wl_pointer_axis axis;
	};
	uint32_t modifier;
	enum tw_binding_type type;
	//this is a private option you need to have for
	uint32_t option;
	struct vtree_node node;

	void *user_data;

	union {
		tw_key_binding key_binding;
		tw_btn_binding btn_binding;
		tw_axis_binding axis_binding;
	};
};


//how you are supposed to define the grabs


void tw_binding_node_init(struct tw_binding_node *node);
void tw_binding_destroy_nodes(struct tw_binding_node *root);

bool
tw_binding_add_key(struct tw_binding_node *root,
			 const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
			 const tw_key_binding binding, uint32_t option,
			 const void *data);

//it is normally implemented like this, unless you have grabs,
bool
tw_binding_add_btn(struct tw_binding_node *root,
			 const struct tw_btn_press presses[MAX_KEY_SEQ_LEN],
			 const tw_btn_binding binding, uint32_t option,
			 const void *data);

bool
tw_binding_add_axis(struct tw_binding_node *root,
			  const struct tw_axis_motion *motion,
			  const tw_axis_binding binding, uint32_t option,
			  const void *data);

void
tw_bindings_apply_to_compositor(struct tw_binding_node *root,
			     struct weston_compositor *ec);


//giving parsing ability to the input system
bool parse_one_press(const char *code_str, const enum tw_binding_type type,
		     struct tw_press *press);


#ifdef  __cplusplus
}
#endif


#endif

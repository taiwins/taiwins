#ifndef TW_BINDINGS_H
#define TW_BINDINGS_H

#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <compositor.h>

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

struct tw_press {
	union {
		xkb_keycode_t keycode;
		uint32_t btn;
		enum wl_pointer_axis axis;
		bool tch;
	};
	uint32_t modifier;
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


struct tw_press tw_bindings_parse_str(const char *code_str, const enum tw_binding_type type);


#ifdef  __cplusplus
}
#endif


#endif

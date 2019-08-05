#include "bindings.h"
#include <tree.h>

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
};


struct tw_bindings {
	//root node for keyboard
	struct tw_binding_node *root_node;
	struct weston_compositor *ec;
};



struct tw_bindings *
tw_bindings_create(struct weston_compositor *ec)
{
	struct tw_bindings *root = malloc(sizeof(struct tw_bindings));
	if (root) {
		root->ec = ec;
		root->root_node = malloc(sizeof(struct tw_binding_node));
		if (!root->root_node) {
			free(root);
			return NULL;
		}
		vtree_node_init(&(root->root_node->node),
				offsetof(struct tw_binding_node, node));
	}
	return root;
}


void
tw_bindings_destroy(struct tw_bindings *bindings)
{
	vtree_destroy(&bindings->root_node->node, free);
}

bool tw_bindings_add_axis(struct tw_bindings *root,
			  const struct tw_axis_motion *motion,
			  const tw_axis_binding binding,
			  void *data)
{
	struct weston_binding *b =
		weston_compositor_add_axis_binding(root->ec, motion->axis_event,
						   motion->modifier, binding, data);
	return (b) ? true : false;

}

bool
tw_bindings_add_btn(struct tw_bindings *root,
		    const struct tw_btn_press *press,
		    const tw_btn_binding binding,
		    void *data)
{
	struct weston_binding *b =
		weston_compositor_add_button_binding(root->ec,
						     press->btn, press->modifier,
						     binding, data);
	return (b) ? true : false;
}



bool
tw_bindings_add_key(struct tw_bindings *root,
		    const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
		    const tw_key_binding binding, uint32_t option,
		    void *data)
{
	return false;
}



static void
tw_keybinding_modifiers(struct weston_keyboard_grab *grab,
			uint32_t serial, uint32_t mods_depressed,
			uint32_t mods_latched,
			uint32_t mods_locked, uint32_t group)
{
	//Do nothing here, the weston_keyboard is already updated
}


struct weston_keyboard_grab_interface tw_keybinding_grab = {
	.key = NULL,
	.modifiers = tw_keybinding_modifiers,
	.cancel = NULL,
};

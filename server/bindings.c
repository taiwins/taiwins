#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-server.h>

#include <tree.h>
#include <compositor.h>
#include "bindings.h"


static inline xkb_keycode_t
kc_linux2xkb(uint32_t kc_linux)
{
	//this should only work on x11, but very weird it works all the time
	return kc_linux+8;
}

static inline uint32_t
kc_xkb2linux(xkb_keycode_t keycode)
{
	return keycode - 8;
}


static uint32_t
modifier_mask_from_xkb_state(struct xkb_state *state)
{
	uint32_t mask = 0;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
		mask |= MODIFIER_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
		mask |= MODIFIER_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
		mask |= MODIFIER_SUPER;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
		mask |= MODIFIER_SHIFT;
	return mask;
}




struct tw_binding_node {
	union {
		xkb_keycode_t keycode;
		uint32_t btn;
		enum wl_pointer_axis axis;
	};
	uint32_t modifier;
	//this is a private option you need to have for
	uint32_t option;
	struct vtree_node node;

	void *user_data;
	tw_key_binding key_binding;

};


struct tw_bindings {
	//root node for keyboard
	struct tw_binding_node root_node;
	struct weston_compositor *ec;
	vector_t apply_list;
};



//////////////////////////////////////////////////////////////////////////////
// keybidning_grab interface
//////////////////////////////////////////////////////////////////////////////
struct keybinding_container {
	struct weston_keyboard_grab grab;
	struct tw_binding_node *node;
};


static void
tw_keybinding_key(struct weston_keyboard_grab *grab,
		  const struct timespec *time, uint32_t key, uint32_t state)
{
	struct keybinding_container *container =
		container_of(grab, struct keybinding_container, grab);
	struct tw_binding_node *tree = container->node;
	struct weston_keyboard *keyboard = grab->keyboard;
	//we get it twice
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	xkb_keycode_t keycode = kc_linux2xkb(key);
	uint32_t mod = modifier_mask_from_xkb_state(keyboard->xkb_state.state);

	bool hit = false;
	for (int i = 0; i < vtree_len(&tree->node); i++) {
		struct tw_binding_node *node =
			vtree_container(vtree_ith_child(&tree->node, i));
		//found our node
		if (node->keycode == keycode && node->modifier == mod) {
			container->node = node;
			hit = true;
			if (vtree_len(&node->node) == 0) {
				node->key_binding(keyboard, time, key,
						  node->option, node->user_data);
				grab->interface->cancel(grab);
			}
			break;
		}
	}
	if (!hit)
		grab->interface->cancel(grab);
}

static void
tw_keybinding_modifiers(struct weston_keyboard_grab *grab,
			uint32_t serial, uint32_t mods_depressed,
			uint32_t mods_latched,
			uint32_t mods_locked, uint32_t group)
{
	//Do nothing here, the weston_keyboard is already updated
}

static void
tw_keybinding_cancel(struct weston_keyboard_grab *grab)
{
	struct keybinding_container *container =
		container_of(grab, struct keybinding_container, grab);
	weston_keyboard_end_grab(grab->keyboard);
	free(container);
}

static struct weston_keyboard_grab_interface tw_keybinding_grab = {
	.key = tw_keybinding_key,
	.modifiers = tw_keybinding_modifiers,
	.cancel = tw_keybinding_cancel,
};


static void
tw_start_keybinding(struct weston_keyboard *keyboard,
		    const struct timespec *time,
		    uint32_t key,
		    void *data)
{
	//if you read the code of libweston, in the function
	//weston_compositor_run_key_binding it does a little trick. Because the
	//grab is called right after, so it installs a special grab to swallow
	//the key and free it. But this only happens if and only if user does
	//not install a grab in the keybinding, which is what we are exactly
	//doing here.

	//so when ever the keybinding is called here, we know for sure we want
	//to start a key binding sequence now.

	//The grab is very powerful, you can use it to implement things like
	//double click
	struct tw_bindings *bindings = data;
	struct keybinding_container *container =
		malloc(sizeof(struct keybinding_container));
	container->node = &bindings->root_node;
	container->grab.interface = &tw_keybinding_grab;
	weston_keyboard_start_grab(keyboard,
				   &container->grab);
}


/////////////////////////////////////////////////////////////////////
//tw_bindings
/////////////////////////////////////////////////////////////////////
struct tw_bindings *
tw_bindings_create(struct weston_compositor *ec)
{
	struct tw_bindings *root = malloc(sizeof(struct tw_bindings));
	if (root) {
		root->ec = ec;
		vtree_node_init(&root->root_node.node,
				offsetof(struct tw_binding_node, node));
	}
	vector_init_zero(&root->apply_list, sizeof(struct taiwins_binding), NULL);
	return root;
}


void
tw_bindings_destroy(struct tw_bindings *bindings)
{
	vtree_destroy_children(&bindings->root_node.node, free);
	if (bindings->apply_list.elems)
		vector_destroy(&bindings->apply_list);
	free(bindings);
}

static inline struct tw_binding_node *
make_binding_node(xkb_keycode_t code, uint32_t mod, uint32_t option,
		  tw_key_binding fuc, const void *data, bool end)
{
	//allocate new ones
	struct tw_binding_node *binding = malloc(sizeof(struct tw_binding_node));
	vtree_node_init(&binding->node, offsetof(struct tw_binding_node, node));
	binding->keycode = code;
	binding->modifier = mod;
	if (end) {
		binding->key_binding = fuc;
		binding->option = option;
		binding->user_data = (void *)data;
	}
	else {
		//internal node
		binding->key_binding = NULL;
		binding->user_data = NULL;
		binding->option = 0;
	}
	return binding;
}

static inline bool
tw_key_presses_end(const struct tw_key_press presses[MAX_KEY_SEQ_LEN], int i)
{
	return (i == MAX_KEY_SEQ_LEN-1 ||
		presses[i+1].keycode == KEY_RESERVED);

}

bool tw_bindings_add_axis(struct tw_bindings *root,
			  const struct tw_axis_motion *motion,
			  const tw_axis_binding binding,
			  void *data)
{
	struct taiwins_binding *new_binding = vector_newelem(&root->apply_list);
	new_binding->type = TW_BINDING_axis;
	new_binding->axis_func = binding;
	new_binding->axisaction = *motion;
	new_binding->user_data = data;
	return true;
}

bool
tw_bindings_add_btn(struct tw_bindings *root,
		    const struct tw_btn_press *press,
		    const tw_btn_binding binding,
		    void *data)
{
	struct taiwins_binding *new_binding = vector_newelem(&root->apply_list);
	new_binding->type = TW_BINDING_btn;
	new_binding->btn_func = binding;
	new_binding->btnpress = *press;
	new_binding->user_data = data;
	return true;
}

bool
tw_bindings_add_touch(struct tw_bindings *root,
		      enum weston_keyboard_modifier modifier,
		      const tw_touch_binding binding,
		      void *data)
{
	struct taiwins_binding *new_binding = vector_newelem(&root->apply_list);
	new_binding->type = TW_BINDING_tch;
	new_binding->touch_func = binding;
	new_binding->btnpress.modifier = modifier;
	new_binding->user_data = data;
	return true;
}


/**
 * /brief add_key_bindings
 *
 * going through the tree structure to add leaves. We are taking the
 * considerration of collisions, this is IMPORTANT because we can only activate
 * one grab at a time.
 *
 * If there are collisions, the keybinding is not not added to the system.
 */
bool
tw_bindings_add_key(struct tw_bindings *root,
		    const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
		    const tw_key_binding func, uint32_t option,
		    void *data)
{
	struct tw_binding_node *subtree = &root->root_node;
	for (int i = 0; i < MAX_KEY_SEQ_LEN; i++) {
		uint32_t mod = presses[i].modifier;
		uint32_t linux_code = presses[i].keycode;
		xkb_keycode_t code = kc_linux2xkb(linux_code);
		int hit = -1;

		if (linux_code == KEY_RESERVED)
			break;

		for (int j = 0; j < vtree_len(&subtree->node); j++) {
			//find the collisions
			struct tw_binding_node *binding =
				vtree_container(vtree_ith_child(&subtree->node, j));
			if (mod == binding->modifier &&
			    code == binding->keycode) {
				hit = j;
				break;
			}
		}
		if (hit == -1 && i == 0) {
			struct taiwins_binding *new_binding = vector_newelem(&root->apply_list);
			new_binding->type = TW_BINDING_key;
			new_binding->keypress[0] = presses[0];
		}
		if (hit == -1) {
			//add node to the system
			struct tw_binding_node *binding =
				make_binding_node(code, mod, option, func, data,
						  tw_key_presses_end(presses, i));
			vtree_node_add_child(&subtree->node, &binding->node);
			subtree = binding;

		} else {
			//test of collisions
			struct tw_binding_node *similar =
				vtree_container(vtree_ith_child(&subtree->node, hit));
			//case 0: both internal node, we continue
			if (!tw_key_presses_end(presses, i) &&
			    similar->key_binding == NULL) {
				subtree = similar;
				continue;
			}
			//case 1: Oops. We are on the internal node, tree is not
			//case 2: Oops. We are on the end node, tree is on the internal.
			//case 3: Oops. We are both on the end node.
			else {
				return false;
			}
		}
	}
	return true;
}

void
tw_bindings_apply(struct tw_bindings *root)
{
	//vector_for_each_safe
	struct taiwins_binding *b;
	vector_for_each(b, &root->apply_list) {
		switch (b->type) {
		case TW_BINDING_key:
			weston_compositor_add_key_binding(
				root->ec, b->keypress[0].keycode,
				b->keypress[0].modifier,
				tw_start_keybinding, root);
			break;
		case TW_BINDING_axis:
			weston_compositor_add_axis_binding(
				root->ec, b->axisaction.axis_event,
				b->axisaction.modifier, b->axis_func,
				b->user_data);
			break;
		case TW_BINDING_btn:
			weston_compositor_add_button_binding(
				root->ec,
				b->btnpress.btn, b->btnpress.modifier,
				b->btn_func, b->user_data);
			break;
		case TW_BINDING_tch:
			weston_compositor_add_touch_binding(
				root->ec, b->btnpress.modifier,
				b->touch_func, b->user_data);
			break;
		case TW_BINDING_INVALID:
			break;
		}
	}
	vector_destroy(&root->apply_list);
	vector_init_zero(&root->apply_list,
			 sizeof(struct taiwins_binding), NULL);
}


static void print_node(const struct vtree_node *n)
{
	const struct tw_binding_node *node = container_of(n, const struct tw_binding_node, node);
	fprintf(stderr, "%d, %d\n", node->keycode-8, node->modifier);
}

void
tw_bindings_print(struct tw_bindings *root)
{
	vtree_print(&root->root_node.node, print_node, 0);
}

#include <stdbool.h>
#include <compositor.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sequential.h>
#include <tree.h>
#include <unistd.h>
#include "input.h"

struct tw_press {
	union {
		xkb_keycode_t keycode;
		uint32_t btn;
		enum wl_pointer_axis axis;
	};
	uint32_t modifier;
};

static inline bool
tw_press_eq(struct tw_press *a, struct tw_press *b)
{
	return a->keycode == b->keycode && a->modifier == b->modifier;
}


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


void tw_binding_node_init(struct tw_binding_node *node)
{
	node->keycode = 0;
	node->modifier = 0;
	node->type = TW_BINDING_key;
	vtree_node_init(&node->node, offsetof(struct tw_binding_node, node));
	node->key_binding = NULL;
}

/////////////////////////////////// RUN BINDINGS ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static struct tw_binding_node *
run_binding(struct tw_binding_node *subtree, enum tw_binding_type type, void *dev,
	    uint32_t mod_mask, uint32_t press, struct weston_pointer_axis_event *event)
{
	struct tw_binding_node *node = subtree;
	bool hit = false;
	for (int i = 0; i < subtree->node.children.len; i++) {
		struct vtree_node *tnode = vtree_ith_child(&subtree->node, i);
		struct tw_binding_node *binding = (struct tw_binding_node *)
			container_of(tnode, struct tw_binding_node, node);
		//take advantage of the union
		hit = binding->keycode == press;
		if (mod_mask !=  binding->modifier ||
		    binding->type != type || !hit)
			continue;
		//test about binding
		node = (binding->key_binding) ? subtree : binding;
		//now run the bindings
		if (type == TW_BINDING_key && binding->key_binding) {
			binding->key_binding((struct weston_keyboard *)dev,
					     binding->option, binding->user_data);
			break;
		}
		else if (type == TW_BINDING_btn && binding->btn_binding) {
			binding->btn_binding((struct weston_pointer *)dev,
					     binding->option, binding->user_data);
			break;
		}
		else if (type == TW_BINDING_axis && binding->axis_binding) {
			binding->axis_binding((struct weston_pointer *)dev,
					      binding->option, event,
					      binding->user_data);
			break;
		}
	}
	return node;
}

static void
run_key_binding(struct weston_keyboard *keyboard, const struct timespec *time,
		uint32_t key, void *data)
{
	static struct tw_binding_node *subtree = NULL;

	if (subtree == NULL)
		subtree = data;
	xkb_keycode_t keycode = kc_linux2xkb(key);
	uint32_t mod_mask = modifier_mask_from_xkb_state(keyboard->xkb_state.state);
	subtree = run_binding(subtree, TW_BINDING_key, keyboard,
			      mod_mask, keycode, NULL);
}


static void
run_btn_binding(struct weston_pointer *pointer, const struct timespec *time,
		uint32_t button, void *data)
{
	static struct tw_binding_node *subtree = NULL;
	//not sure if this really works
	struct weston_keyboard *keyboard = pointer->seat->keyboard_state;
	uint32_t mod_mask = keyboard ?
		modifier_mask_from_xkb_state(keyboard->xkb_state.state) :
		0;
	if (!subtree)
		subtree = data;
	subtree = run_binding(subtree, TW_BINDING_btn, pointer,
			      mod_mask, button, NULL);
}


static void
run_axis_binding(struct weston_pointer *pointer,
		      const struct timespec *time,
		      struct weston_pointer_axis_event *event,
		      void *data)
{
	static struct tw_binding_node *subtree = NULL;
	if (!subtree)
		subtree = data;
	//not sure if this really works
	struct weston_keyboard *keyboard = pointer->seat->keyboard_state;
	uint32_t mod_mask = keyboard ?
		modifier_mask_from_xkb_state(keyboard->xkb_state.state) :
		0;
	subtree = run_binding(subtree, TW_BINDING_axis, pointer,
			      mod_mask, event->axis, event);
}

static void
run_touch_binding(struct weston_touch *touch,
		       const struct timespec *time,
		       void *data)
{
	static struct tw_binding_node *subtree = NULL;
	struct weston_keyboard *keyboard = touch->seat->keyboard_state;
	uint32_t mod_mask = keyboard ?
		modifier_mask_from_xkb_state(keyboard->xkb_state.state) : 0;
	if (!subtree)
		subtree = data;

	subtree = run_binding(subtree, TW_BINDING_tch, touch, mod_mask, 0, NULL);
}


/////////////////////////////////// ADD BINDINGS ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
static inline bool
is_end_key_seq(const struct tw_key_press presses[MAX_KEY_SEQ_LEN], int i)
{
	//keycode cannot be zero, it starts from 8
	return (i == MAX_KEY_SEQ_LEN-1 ||
		presses[i+1].keycode == 0);
}

static inline struct tw_binding_node *
make_key_binding_node(xkb_keycode_t code, uint32_t mod, uint32_t option,
		  tw_key_binding fuc, const void *data, bool end)
{
	//allocate new ones
	struct tw_binding_node *binding = malloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(binding);
	binding->keycode = code;
	binding->modifier = mod;
	binding->type = TW_BINDING_key;
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


bool
tw_binding_add_key(struct tw_binding_node *root, struct weston_keyboard *keyboard,
			 const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
			 const tw_key_binding fuc, uint32_t option,
			 const void *data)
{
	struct tw_binding_node *subtree = root;
	for (int i = 0; i < MAX_KEY_SEQ_LEN; i++) {
		uint32_t mod = presses[i].modifier;
		xkb_keycode_t code = presses[i].keycode;
		int hit = -1;

		if (code == 0)
			break;
		for (int j = 0; j < subtree->node.children.len; j++) {
			struct vtree_node *tnode = vtree_ith_child(&subtree->node, j);
			struct tw_binding_node *binding = (struct tw_binding_node *)
				container_of(tnode, struct tw_binding_node, node);
			if (binding->type == TW_BINDING_key &&
			    binding->keycode == code &&
			    binding->modifier == mod) {
				hit = j;
				break;
			}
		}
		if (hit == -1) {
			struct tw_binding_node *binding =
				make_key_binding_node(code, mod, option, fuc, data,
						  is_end_key_seq(presses, i));
			vtree_node_add_child(&subtree->node, &binding->node);
			subtree = binding;
		}
		else {
			//test of collisions
			struct vtree_node *tnode = vtree_ith_child(&subtree->node, hit);
			struct tw_binding_node *binding = (struct tw_binding_node *)
				container_of(tnode, struct tw_binding_node, node);
			//case 0: both internal node, we skip
			if (!is_end_key_seq(presses, i) && binding->key_binding == NULL) {
				subtree = binding;
				continue;
			}
			else { //case 1: Oops. We are on the internal node, tree is not
				//case 2: Oops. We are on the end node, tree is on the internal.
				//case 3: Oops. We are both on the end node.
				return false;
			}
		}
	}
	return true;
}


bool
tw_binding_add_axis(struct tw_binding_node *root,
			  const struct tw_axis_motion *motion,
			  const tw_axis_binding binding, uint32_t option,
			  const void *data)
{
	return false;
}

bool
tw_binding_add_btn(struct tw_binding_node *root,
			 const struct tw_btn_press presses[MAX_KEY_SEQ_LEN],
			 const tw_btn_binding binding, uint32_t option,
			 const void *data)
{
	return false;
}

static void
cache_input(const struct vtree_node *node, void *data)
{
	vector_t *v = data;
	const struct tw_binding_node *t = container_of(node, const struct tw_binding_node, node);
	struct tw_press p = {
		.keycode = t->keycode,
		.modifier = t->modifier,
	};
	if (p.keycode == 0)
		return;
	for (int i = 0; i < v->len; i++) {
		struct tw_press *k = vector_at(v, i);
		if (tw_press_eq(&p, k))
		    return;
	}
	vector_append(v, &p);
}

void
tw_input_apply_to_compositor(const struct tw_binding_node *root,
			     struct weston_compositor *ec)
{
	void *data = (void *)root;
	vector_t v;
	vector_init(&v, sizeof(struct tw_press), NULL);
	//we need to create a local cache to search
	vtree_iterate(&root->node, &v, cache_input);
	if (root->type == TW_BINDING_key) {
		for (int i = 0; i < v.len; i++) {
			struct tw_press *press = vector_at(&v, i);
			weston_compositor_add_key_binding(
				ec, kc_xkb2linux(press->keycode),
				press->modifier, run_key_binding, data);
		}
	} else if (root->type == TW_BINDING_btn) {
		for (int i = 0; i < v.len; i++) {
			struct tw_press *press = vector_at(&v, i);
			weston_compositor_add_button_binding(
				ec, press->btn, press->modifier, run_btn_binding, data);
		}
	} else if (root->type == TW_BINDING_tch) {
		for (int i = 0; i < v.len; i++) {
			struct tw_press *press = vector_at(&v, i);
			weston_compositor_add_touch_binding(
				ec, press->modifier, run_touch_binding,
				data);
		}
	} else if (root->type == TW_BINDING_axis) {
		for (int i = 0; i < v.len; i++) {
			struct tw_press *press = vector_at(&v, i);
			weston_compositor_add_axis_binding(
				ec, press->axis, press->modifier,
				run_axis_binding, data);
		}
	}
	vector_destroy(&v);
}

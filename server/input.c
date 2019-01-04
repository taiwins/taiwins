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

//this one does not work for axis event
static inline bool
tw_press_seq_end(const struct tw_press presses[MAX_KEY_SEQ_LEN], int i)
{
	return (i == MAX_KEY_SEQ_LEN-1 || presses[i+1].keycode == 0);

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

void
tw_binding_destroy_nodes(struct tw_binding_node *root)
{
	vtree_destroy(&root->node, free);
}


/////////////////////////////////// RUN BINDINGS ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

/**
 * /brief general binding handler
 *
 * /return NULL if hit nothing, a sub branch if hit a middle node. At last
 * return the subtree if it hits a binding
 */
static struct tw_binding_node *
run_binding(struct tw_binding_node *subtree, enum tw_binding_type type, void *dev,
	    uint32_t mod_mask, uint32_t press, struct weston_pointer_axis_event *event)
{
	//now you have two
	struct tw_binding_node *node = NULL;
	bool hit = false;
	for (int i = 0; i < subtree->node.children.len; i++) {
		struct vtree_node *tnode = vtree_ith_child(&subtree->node, i);
		struct tw_binding_node *binding = (struct tw_binding_node *)
			container_of(tnode, struct tw_binding_node, node);
		//take advantage of the union
		hit = binding->keycode == press && mod_mask == binding->modifier;
		if (!hit || binding->type != type)
			continue;
		//hit, but test if we are in the internal node
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
	}
	//`if (hit == false)` this could happend. It happens when binding system
	//gives the key does not fit along the binding tree. For example, C-x x
	//causes the system to recongnize both C-x and x as binding. If you only
	//input x. It should be treated as normal input
	return (hit) ? node : NULL;
}


/* we still have one problem here, if last key stroke is in the binding and this
 * one is not. Then it will not get notified. And we are still in the binding zoom
 */
static void
run_key_binding(struct weston_keyboard *keyboard, const struct timespec *time,
		uint32_t key, void *data)
{
	static struct tw_binding_node *subtree = NULL;

	if (subtree == NULL)
		subtree = data;
	/* struct wl_display *display = keyboard->seat->compositor->wl_display; */
	xkb_keycode_t keycode = kc_linux2xkb(key);
	uint32_t mod_mask = modifier_mask_from_xkb_state(keyboard->xkb_state.state);
	struct tw_binding_node *pass =
		run_binding(subtree, TW_BINDING_key, keyboard,
			    mod_mask, keycode, NULL);
	//we have a confusing part of the code, because
	//if we didn't hit anything, we pass the event donw
	if (!pass) {
		//we maybe should deal with the modifiers as well
		weston_keyboard_send_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
		weston_keyboard_send_key(keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
		subtree = NULL;
	} //case 1: if we hit a binding
	else if (pass == subtree)
		subtree = data;
	else //case 2/3
		subtree = pass;
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

	struct tw_binding_node *pass =
		run_binding(subtree, TW_BINDING_btn, pointer,
			    mod_mask, button, NULL);
	if (!pass) {
		weston_pointer_send_button(pointer, time, button, WL_POINTER_BUTTON_STATE_PRESSED);
		weston_pointer_send_button(pointer, time, button, WL_POINTER_BUTTON_STATE_RELEASED);
		subtree = NULL;
	}
	if (pass == subtree) //hit
		subtree = data;
	else //internal node
		subtree = pass;
}


static void
run_axis_binding(struct weston_pointer *pointer,
		      const struct timespec *time,
		      struct weston_pointer_axis_event *event,
		      void *data)
{
	struct tw_binding_node *root = data;
	//not sure if this really works
	struct weston_keyboard *keyboard = pointer->seat->keyboard_state;
	uint32_t mod_mask = keyboard ?
		modifier_mask_from_xkb_state(keyboard->xkb_state.state) :
		0;
	uint32_t axis = event->axis;
	uint32_t idx = mod_mask + (axis << 4);
	struct vtree_node *n = *(struct vtree_node **)
		vector_at(&root->node.children, idx);
	if (n) {
		struct tw_binding_node *binding =
			container_of(n, struct tw_binding_node, node);
		binding->axis_binding(pointer, binding->option, event,
				      binding->user_data);
	}
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
	struct tw_binding_node *pass =
		run_binding(subtree, TW_BINDING_tch, touch, mod_mask, 0, NULL);
	if (pass == subtree)
		subtree = data;
	else
		subtree = pass;
}


/////////////////////////////////// ADD BINDINGS ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

static inline struct tw_binding_node *
make_binding_node(xkb_keycode_t code, uint32_t mod, uint32_t option,
		  tw_key_binding fuc, const void *data, bool end, enum tw_binding_type type)
{
	//allocate new ones
	struct tw_binding_node *binding = malloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(binding);
	binding->keycode = code;
	binding->modifier = mod;
	binding->type = type;
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

/* this does not work for axis */
static bool
tw_binding_add_seq(struct tw_binding_node *root,
		   const struct tw_press presses[MAX_KEY_SEQ_LEN],
		   const tw_key_binding fuc, uint32_t option,
		   enum tw_binding_type type, const void *data)
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
				make_binding_node(code, mod, option, fuc, data,
						  tw_press_seq_end(presses, i), type);
			vtree_node_add_child(&subtree->node, &binding->node);
			subtree = binding;
		}
		else {
			//test of collisions
			struct vtree_node *tnode = vtree_ith_child(&subtree->node, hit);
			struct tw_binding_node *binding = (struct tw_binding_node *)
				container_of(tnode, struct tw_binding_node, node);
			//case 0: both internal node, we skip
			if (!tw_press_seq_end(presses, i) && binding->key_binding == NULL) {
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
tw_binding_add_key(struct tw_binding_node *root,
			 const struct tw_key_press presses[MAX_KEY_SEQ_LEN],
			 const tw_key_binding fuc, uint32_t option,
			 const void *data)
{
	struct tw_press cp_presses[MAX_KEY_SEQ_LEN] = {
		{{presses[0].keycode}, presses[0].modifier},
		{{presses[1].keycode}, presses[1].modifier},
		{{presses[2].keycode}, presses[2].modifier},
		{{presses[3].keycode}, presses[3].modifier},
		{{presses[4].keycode}, presses[4].modifier},
	};
	return tw_binding_add_seq(root, cp_presses, fuc, option, TW_BINDING_key, data);
}

bool
tw_binding_add_btn(struct tw_binding_node *root,
			 const struct tw_btn_press presses[MAX_KEY_SEQ_LEN],
			 const tw_btn_binding func, uint32_t option,
			 const void *data)
{
	//it works exactly like
	struct tw_press cp_presses[MAX_KEY_SEQ_LEN] = {
	    {{presses[0].btn}, presses[0].modifier},
	    {{presses[1].btn}, presses[1].modifier},
	    {{presses[2].btn}, presses[2].modifier},
	    {{presses[3].btn}, presses[3].modifier},
	    {{presses[4].btn}, presses[4].modifier},
	};
	return tw_binding_add_seq(root, cp_presses, (const tw_key_binding)func, option,
				  TW_BINDING_btn, data);
}

bool
tw_binding_add_axis(struct tw_binding_node *root,
		    const struct tw_axis_motion *motion,
		    const tw_axis_binding func, uint32_t option,
		    const void *data)
{
	//axis binding should be only one level
	struct tw_binding_node *binding = malloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(binding);
	binding->axis = motion->axis_event;
	binding->modifier = motion->modifier;
	binding->type = TW_BINDING_axis;
	binding->axis_binding = func;
	binding->option = option;
	binding->user_data = (void *)data;
	vtree_node_add_child(&root->node, &binding->node);
	return true;
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
tw_bindings_apply_to_compositor(struct tw_binding_node *root,
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
		struct vtree_node *nodes[32] = {0};
		for (int i = 0; i < root->node.children.len; i++) {
			struct vtree_node *n = vtree_ith_child(&root->node, i);
			struct tw_binding_node *b = container_of(n, struct tw_binding_node, node);
			size_t idx = (b->axis << 4) + b->modifier;
			nodes[idx] = n;
			weston_compositor_add_axis_binding(ec, b->axis, b->modifier, run_axis_binding, data);
		}
		//resort it so we can have enough space to have immediate access
		vector_resize(&root->node.children, 32);
		for (int i = 0; i < 32; i++)
			*(struct vtree_node **)vector_at(&root->node.children, i) =
				(nodes[i]) ? nodes[i] : NULL;
	}
	vector_destroy(&v);
}

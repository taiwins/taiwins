#include <stdbool.h>
#include <compositor.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sequential.h>
#include <tree.h>
#include <unistd.h>
#include "input.1.h"


static inline xkb_keycode_t
kc_linux2xkb(uint32_t kc_linux)
{
	//this should only work on x11, but very weird it works all the time
	return kc_linux+8;
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
	node->keysym = 0;
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
		hit = binding->keysym == press;
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
run_key_binding(struct weston_keyboard *keyboard, uint32_t time,
		uint32_t key, void *data)
{
	static struct tw_binding_node *subtree = NULL;

	if (subtree == NULL)
		subtree = data;
	xkb_keycode_t keycode = kc_linux2xkb(key);
	xkb_keysym_t keysym =
		xkb_state_key_get_one_sym(keyboard->xkb_state.state,
			keycode);
	uint32_t mod_mask = modifier_mask_from_xkb_state(keyboard->xkb_state.state);
	subtree = run_binding(subtree, TW_BINDING_key, keyboard,
			      mod_mask, keysym, NULL);
}


void
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


void run_axis_binding(struct weston_pointer *pointer,
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

void run_touch_binding(struct weston_touch *touch,
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
	return (i == MAX_KEY_SEQ_LEN-1 ||
		presses[i+1].keysym == XKB_KEY_NoSymbol);
}

static inline struct tw_binding_node *
make_key_binding_node(xkb_keysym_t sym, uint32_t mod, uint32_t option,
		  tw_key_binding fuc, const void *data, bool end)
{
	//allocate new ones
	struct tw_binding_node *binding = malloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(binding);
	binding->keysym = sym;
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
		uint32_t mod = presses[i].modiifer;
		xkb_keysym_t sym = presses[i].keysym;
		int hit = -1;

		if (sym == XKB_KEY_NoSymbol)
			break;
		for (int j = 0; j < subtree->node.children.len; j++) {
			struct vtree_node *tnode = vtree_ith_child(&subtree->node, j);
			struct tw_binding_node *binding = (struct tw_binding_node *)
				container_of(tnode, struct tw_binding_node, node);
			if (binding->type == TW_BINDING_key &&
			    binding->keysym == sym &&
			    binding->modifier == mod) {
				hit = j;
				break;
			}
		}
		if (hit == -1) {
			struct tw_binding_node *binding =
				make_key_binding_node(sym, mod, option, fuc, data,
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


void
tw_input_apply_to_compositor(const struct tw_binding_node *root,
			     struct weston_compositor *ec)
{
	//we need to create a local cache to search
//	for (int i = 0; i < root->)
}

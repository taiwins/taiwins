#include <stdbool.h>
#include <compositor.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <sequential.h>
#include <tree.h>
#include <unistd.h>

#include "input.h"

extern struct weston_seat *seat0;

enum tw_modifier_mask {
	TW_NOMOD = 0,
	TW_ALT = 1,
	TW_CTRL = 2,
	TW_SUPER = 4
};

static uint32_t
modifier_mask_from_xkb_state(struct xkb_state *state)
{
	uint32_t mask = TW_NOMOD;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_SUPER;
	return mask;
}

static uint32_t
modifier_mask_from_weston_mod(uint32_t mod)
{
	uint32_t mask = 0;
	if (mod & MODIFIER_ALT)
		mask |= TW_ALT;
	if (mod & MODIFIER_CTRL)
		mask |= TW_CTRL;
	if (mod & MODIFIER_SUPER)
		mask |= TW_SUPER;
	return mask;
}



static struct tw_keymap_tree keybinding_root = {
	.keysym = 0,
	.modifier = 0,
	.node = {
		.children = {
			.elemsize = sizeof(struct tw_keymap_tree), //this is vital!
			.len = 0,
			.alloc_len = 0
		},
		.parent = NULL,
		.offset = offsetof(struct tw_keymap_tree, node),
	},
	.keyfun = NULL
};

//static struct tw_keymap_tree *root;
#define node2treekeymap(ptr)	container_of(ptr, struct tw_keymap_tree, node)


static uint32_t
kc_linux2xkb(uint32_t kc_linux)
{
	//this should only work on x11, but very weird it works all the time
	return kc_linux+8;
}

static uint32_t
kc_xkb2linux(uint32_t kc_xkb)
{
	return kc_xkb-8;
}

/**
 *
 * @brief insert the keybing seq in the tree (C-x, C-r, C-c)
 *
 * The depth of the tree shouldn't be over something like 3. We also need a
 * cache of keysyms in order to update the cache, which requires the hash table
 */
void
update_tw_keymap_tree(const vector_t *keyseq, const shortcut_func_t func)
{
	char keysym_name[64];
	struct tw_keymap_tree *tree = &keybinding_root;
	for (int i = 0; i < keyseq->len; i++) {
		const struct tw_keypress *keypress = (struct tw_keypress *)cvector_at(keyseq, i);
		uint32_t modifiers = modifier_mask_from_weston_mod(keypress->modifiers);
		xkb_keysym_t keysym = keypress->keysym;
		xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));
		//TODO checking the hash table
		bool hit = false;
		for (int j = 0; j < tree->node.children.len; j++) {
			//bindings contains the tree node, for every compound data structures, you need
			struct tw_keymap_tree *binding =
				(struct tw_keymap_tree *)vector_at(&tree->node.children, j);
			if (binding->keysym == keysym && binding->modifier == modifiers) {
				hit = true;
				tree = binding;

				break;
			}
		}
		if (!hit) {
			struct tw_keymap_tree tmpkey = {
				.keysym = keysym,
				.modifier = modifiers,
			};
//			fprintf(stderr, "insert the symbol %s with modifier %d\n", keysym_name, modifiers);
			if (i == keyseq->len-1)
				tmpkey.keyfun = func;
			vtree_node_add_child(&tree->node, &tmpkey.node);
			tree = (struct tw_keymap_tree *)vector_at(&tree->node.children, tree->node.children.len-1);
		}
	}
}


static void
debug_keypress(const void *data)
{
	char name[64];
	const struct tw_keymap_tree *root = (const struct tw_keymap_tree *)data;
	xkb_keysym_get_name(root->keysym, name, sizeof(name));
	fprintf(stderr, "key %s with modifier %d and function %p\n", name, root->modifier, root->keyfun);
}


void
debug_keybindtree(void)
{
	vtree_print(&keybinding_root.node, debug_keypress, 0);
}



void
run_keybinding(struct weston_keyboard *keyboard,
	       uint32_t time, uint32_t key,
	       void *data)
{
	struct tw_keymap_tree *tree = &keybinding_root;
	xkb_keycode_t keycode = kc_linux2xkb(key);
	xkb_keysym_t  keysym  = xkb_state_key_get_one_sym(keyboard->xkb_state.state,
							  keycode);
	uint32_t modifier_mask = modifier_mask_from_xkb_state(keyboard->xkb_state.state);

	bool hit = false;
	for (int i = 0; i < tree->node.children.len; i++) {
		struct tw_keymap_tree *binding =
			(struct tw_keymap_tree *)cvector_at(&tree->node.children, i);
		if (modifier_mask != binding->modifier)
			continue;
		if (binding->keysym == keysym && binding->keyfun) {
			tree = &keybinding_root;
			binding->keyfun();
			hit = true;
			break;
		} else if (binding->keysym == keysym && !binding->keyfun) {
			tree = binding;
			hit = true;
			break;
		}
	}
	if (!hit) {
		tree = &keybinding_root;
	}
}

void
run_keybinding_wayland(struct xkb_state *state,
		       uint32_t time, uint32_t key,
	void *data)
{
	//NOTE xkb itself doesn't have mechnism to detect key up or down
	static struct tw_keymap_tree *tree = &keybinding_root;
	char keyname[64];
	xkb_keycode_t keycode = kc_linux2xkb(key);
	xkb_keysym_t  keysym  = xkb_state_key_get_one_sym(state,
							  keycode);
	uint32_t modifier_mask = modifier_mask_from_xkb_state(state);
	xkb_keysym_get_name( keysym, keyname, sizeof(keyname));
	fprintf(stderr, "%s key with modifier %d\n", keyname, modifier_mask);

	bool hit = false;
	for (int i = 0; i < tree->node.children.len; i++) {
		struct tw_keymap_tree *binding =
			(struct tw_keymap_tree *)cvector_at(&tree->node.children, i);
		if (modifier_mask != binding->modifier)
			continue;
		if (binding->keysym == keysym && binding->keyfun) {
//			fprintf(stderr, "hit!!!! %s key with modifier %d\n", keyname, modifier_mask);
			binding->keyfun();
			hit = true;
			tree = &keybinding_root;
			break;
		} else if (binding->keysym == keysym && !binding->keyfun) {
			tree = binding;
//			fprintf(stderr, "In middle %s key with modifier %d\n and next we have %d children\n", keyname, modifier_mask, tree->node.children.len);
			hit = true;
			break;
		}
	}
	if (!hit) {
//		fprintf(stderr, "I should see you here though.\n");
		tree = &keybinding_root;
	}
}

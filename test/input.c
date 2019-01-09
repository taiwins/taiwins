#include <search.h>
#include <stdbool.h>
#include <math.h>
#include <compositor.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <unistd.h>

#include <sequential.h>
#include <tree.h>
#include <hash.h>

#include "input.h"
extern struct weston_seat *seat0;
//at the layer, you only need to know one key
struct tw_keymap_tree {
	struct vtree_node node;
	xkb_keysym_t keysym;
	uint32_t modifier;
	shortcut_func_t keyfun;
};


enum tw_modifier_mask {
	TW_NOMOD = 0,
	TW_ALT = 1,
	TW_CTRL = 2,
	TW_SUPER = 4,
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


static hash_cmp_val
compare_keypress(const void *a, const void *b)
{
	const struct tw_keypress *ta = (const struct tw_keypress *)a;
	const struct tw_keypress *tb = (const struct tw_keypress *)b;
	if (tb->keysym == 0) //XKB_KEY_NoSymbol
		return hash_empty;
	uint64_t code_a = (uint64_t) ta->modifiers << 32 | ta->keysym;
	uint64_t code_b = (uint64_t) tb->modifiers << 32 | tb->keysym;
	return (code_a == code_b) ? hash_eq : hash_neq;
}

static uint64_t
hash_keypress1(const void *key)
{
	const struct tw_keypress *press = (const struct tw_keypress *)key;
	uint64_t code = (uint64_t)press->modifiers << 32 | press->keysym;
	return code % 5871;
}

static uint64_t
hash_keypress0(const void *key)
{
	const struct tw_keypress *press = (const struct tw_keypress *)key;
	uint64_t code = (uint64_t)press->modifiers << 32 | press->keysym;
	double hash_val = (sqrt(5)-1) / 2 * code;
	return floor(8192 * (hash_val - floor(hash_val)));
}

static dhashtab_t taiwins_keybindings = {
	.data = {
		.elemsize = sizeof(struct tw_keypress),
		.len = 0,
		.alloc_len = 0,
		.elems = NULL
	},
	.cmp = compare_keypress,
	.hash0 = hash_keypress0,
	.hash1 = hash_keypress1
};


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
 * The depth of the tree shouldn't be over something like 3.
 */
void
update_tw_keymap_tree(const vector_t *keyseq, const shortcut_func_t func)
{
	char keysym_name[64];
	struct tw_keymap_tree *tree = &keybinding_root;
	for (int i = 0; i < keyseq->len; i++) {
		const struct tw_keypress *keypress = (struct tw_keypress *)cvector_at(keyseq, i);
		//TODO test it
		uint32_t modifiers = modifier_mask_from_weston_mod(keypress->modifiers);
		xkb_keysym_t keysym = keypress->keysym;
		xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));

		bool hit = false;
		for (int j = 0; j < vtree_len(&tree->node); j++) {
			//bindings contains the tree node, for every compound data structures, you need
			struct tw_keymap_tree *binding = (struct tw_keymap_tree *)vtree_ith_child(&tree->node, j);
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
		}
	}
}


void
update_tw_keypress_cache(const vector_t *keyseq, struct weston_compositor *compositor)
{
	for (int i = 0; i < keyseq->len; i++) {
		const struct tw_keypress *keypress = (struct tw_keypress *)cvector_at(keyseq, i);
		uint32_t modifiers = keypress->modifiers;
		uint32_t keycode = keypress->keycode;
		if (dhash_search(&taiwins_keybindings, keypress))
			continue;
		else {
			char name[64];
			dhash_insert(&taiwins_keybindings, keypress);
			xkb_keysym_get_name(keypress->keysym, name, sizeof(name));
//			weston_compositor_add_key_binding(compositor, keycode, modifiers, run_keybinding, NULL);
			fprintf(stderr, "updating keymap cache keysym %s with code %d and modifier %d\n", name, keycode, modifiers);
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
	for (int i = 0; i < vtree_len(&tree->node); i++) {
		struct tw_keymap_tree *binding = container_of(vtree_ith_child(&tree->node, i),
							      struct tw_keymap_tree, node);
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
	fprintf(stderr, "%s key with code %d and modifier %d\n", keyname, keycode-8, modifier_mask);

	bool hit = false;
	for (int i = 0; i < vtree_len(&tree->node); i++) {
		struct tw_keymap_tree *binding = container_of(vtree_ith_child(&tree->node, i),
							      struct tw_keymap_tree, node);
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

/*
 * bindings.c - taiwins bindings function
 *
 * Copyright (c) 2020 Xichen Zhou
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

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-server.h>
#include <ctypes/tree.h>
#include <ctypes/vector.h>
#include <ctypes/helpers.h>
#include <taiwins/objects/utils.h>

#include "bindings.h"

struct tw_binding_node {
	struct vtree_node node;
	uint32_t keycode;
	uint32_t modifier;
	//this is a private option you need to have for
	bool end;
	struct tw_binding binding;
};

struct tw_bindings {
	//root node for keyboard
	struct wl_display *display;
	struct tw_binding_node root_node;
	struct wl_listener destroy_listener;
	vector_t apply_list;
};


static struct tw_binding_node *
make_binding_node(uint32_t code, uint32_t mod, uint32_t option,
                  const tw_key_binding func, const void *data, bool end)
{
	//allocate a new node
	struct tw_binding_node *node = calloc(1, sizeof(*node));
	vtree_node_init(&node->node, offsetof(struct tw_binding_node, node));
	node->keycode = code;
	node->modifier = mod;
	node->end = end;
	if (end) {
		node->binding.type = TW_BINDING_key;
		node->binding.key_func = func;
		node->binding.option = option;
		node->binding.user_data = (void *)data;
	}
	else {
		//internal node
		node->binding = (struct tw_binding){0};
		node->binding.key_func = NULL;

	}
	return node;
}

static inline bool
key_presses_end(const struct tw_key_press presses[MAX_KEY_SEQ_LEN], int i)
{
	return (i == MAX_KEY_SEQ_LEN-1 ||
		presses[i+1].keycode == KEY_RESERVED);
}

static void
notify_bindings_destroy(struct wl_listener *listener, void *data)
{
	struct tw_bindings *bindings =
		container_of(listener, struct tw_bindings, destroy_listener);
	tw_bindings_destroy(bindings);
}

static void
tw_bindings_release(struct tw_bindings *bindings)
{
	tw_reset_wl_list(&bindings->destroy_listener.link);
	vtree_destroy_children(&bindings->root_node.node, free);
	if (bindings->apply_list.elems)
		vector_destroy(&bindings->apply_list);
}

/******************************************************************************
 * exposed API
 *****************************************************************************/

struct tw_bindings *
tw_bindings_create(struct wl_display *display)
{
	struct tw_bindings *root = calloc(1, sizeof(struct tw_bindings));
	if (root) {
		root->display = display;
		vtree_node_init(&root->root_node.node,
				offsetof(struct tw_binding_node, node));
	}
	vector_init_zero(&root->apply_list,
	                 sizeof(struct tw_binding), NULL);
	tw_set_display_destroy_listener(display, &root->destroy_listener,
	                                notify_bindings_destroy);
	return root;
}

void
tw_bindings_destroy(struct tw_bindings *bindings)
{
	tw_bindings_release(bindings);
	free(bindings);
}

void
tw_bindings_move(struct tw_bindings *dst, struct tw_bindings *src)
{
	struct vtree_node **pnode;

	tw_bindings_release(dst);
	vtree_node_init(&dst->root_node.node,
	                offsetof(struct tw_binding_node, node));
	//a shallow copy
	dst->display = src->display;
	vector_for_each(pnode, &src->root_node.node.children)
		(*pnode)->parent = &dst->root_node.node;
	dst->root_node = src->root_node;
	dst->apply_list = src->apply_list;

	//purge the src list
	vector_init_zero(&src->apply_list, sizeof(struct tw_binding), NULL);
	vtree_node_init(&src->root_node.node,
	                offsetof(struct tw_binding_node, node));
	tw_reset_wl_list(&src->destroy_listener.link);
}

struct tw_binding_node *
tw_binding_node_step(struct tw_binding_node *tree,
                         uint32_t keycode, uint32_t mod_mask)
{
	struct tw_binding_node *node = NULL;

	for (unsigned i = 0; i < vtree_len(&tree->node); i++) {
		node = vtree_container(vtree_ith_child(&tree->node, i));
		if (node->keycode == keycode &&
		    node->modifier == mod_mask)
			return node;
	}
	return NULL;
}

struct tw_binding *
tw_binding_node_get_binding(struct tw_binding_node *state)
{
	return (state && state->end) ? &state->binding : NULL;
}

struct tw_binding_node *
tw_bindings_find_key(struct tw_bindings *bindings,
                     uint32_t key, uint32_t mod_mask)
{
	struct tw_binding_node *root = &bindings->root_node;
	struct tw_binding_node *node = NULL;

	for (unsigned i = 0; i < vtree_len(&root->node); i++) {
		node = vtree_container(vtree_ith_child(&root->node, i));
		if (node->keycode == key && node->modifier == mod_mask) {
			return root;
		}
	}
	return NULL;
}

struct tw_binding *
tw_bindings_find_btn(struct tw_bindings *bindings, uint32_t btn,
                     uint32_t mod_mask)
{
	struct tw_binding *binding = NULL;
	vector_for_each(binding, &bindings->apply_list) {
		if (binding->type == TW_BINDING_btn &&
		    binding->btnpress.btn == btn &&
		    binding->btnpress.modifier == mod_mask)
			return binding;
	}
	return NULL;
}

struct tw_binding *
tw_bindings_find_axis(struct tw_bindings *bindings,
                      enum wl_pointer_axis action, uint32_t mod_mask)
{
	struct tw_binding *binding = NULL;
	vector_for_each(binding, &bindings->apply_list) {
		if (binding->type == TW_BINDING_axis &&
		    binding->axisaction.modifier == mod_mask &&
		    binding->axisaction.axis_event == action)
			return binding;
	}
	return NULL;

}

struct tw_binding *
tw_bindings_find_touch(struct tw_bindings *bindings, uint32_t mod_mask)
{
	struct tw_binding *binding = NULL;
	vector_for_each(binding, &bindings->apply_list) {
		if (binding->type == TW_BINDING_tch &&
		    binding->touch.modifier == mod_mask)
			return binding;
	}
	return NULL;

}



bool tw_bindings_add_axis(struct tw_bindings *root,
			  const struct tw_axis_motion *motion,
			  const tw_axis_binding binding,
			  void *data)
{
	struct tw_binding *new_binding = vector_newelem(&root->apply_list);
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
	struct tw_binding *new_binding = vector_newelem(&root->apply_list);
	new_binding->type = TW_BINDING_btn;
	new_binding->btn_func = binding;
	new_binding->btnpress = *press;
	new_binding->user_data = data;
	return true;
}

bool
tw_bindings_add_touch(struct tw_bindings *root,
                      uint32_t modifiers,
		      const tw_touch_binding binding,
		      void *data)
{
	struct tw_binding *new_binding = vector_newelem(&root->apply_list);
	new_binding->type = TW_BINDING_tch;
	new_binding->touch_func = binding;
	new_binding->touch.modifier = modifiers;
	new_binding->user_data = data;
	return true;
}

/**
 * @brief add_key_bindings
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
		uint32_t code = presses[i].keycode;
		int hit = -1;
		struct tw_binding_node *binding;

		if (code == KEY_RESERVED)
			break;

		for (unsigned j = 0; j < vtree_len(&subtree->node); j++) {
			//find the collisions
			 binding = vtree_container(
				 vtree_ith_child(&subtree->node, j));
			if (mod == binding->modifier &&
			    code == binding->keycode) {
				hit = j;
				break;
			}
		}
		if (hit == -1) {
			//add node to the system
			struct tw_binding_node *binding =
				make_binding_node(code, mod, option,
				                  func, data,
				                  key_presses_end(presses, i));
			vtree_node_add_child(&subtree->node, &binding->node);
			subtree = binding;

		} else {
			//test of collisions
			struct tw_binding_node *similar = vtree_container(
				vtree_ith_child(&subtree->node, hit));
			//case 0: both internal node, we continue
			if (!key_presses_end(presses, i) && !similar->end) {
				subtree = similar;
				continue;
			}
			//case 1: Oops. We are on the internal node, tree is not
			//case 2: Oops. We are on the end node, tree is on the
			//internal.
			//case 3: Oops. We are both on the end node.
			else {
				return false;
			}
		}
	}
	return true;
}

static void
print_node(const struct vtree_node *n)
{
	const struct tw_binding_node *node =
		container_of(n, const struct tw_binding_node, node);
	fprintf(stderr, "%d, %d\n", node->keycode-8, node->modifier);
}

void
tw_bindings_print(struct tw_bindings *root)
{
	vtree_print(&root->root_node.node, print_node, 0);
}

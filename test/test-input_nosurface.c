#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <search.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <linux/input.h>
#include <compositor.h>
#include <sequential.h>

#include "input.1.h"


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

void print_key(struct weston_keyboard *keyboard, uint32_t option, void *data)
{
	fprintf(stderr, "hey, look, we just pressed %c!\n", option);
}

void print_tree_key(const struct vtree_node *node)
{
	const struct tw_binding_node *binding =
		container_of(node, const struct tw_binding_node, node);
	fprintf(stderr, "%s-%c\n",  (binding->modifier == MODIFIER_CTRL) ? "Ctrl" : "No",
		binding->option);
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

static struct tw_key_press cxx[MAX_KEY_SEQ_LEN] = {
	{KEY_X+8, MODIFIER_CTRL},
	{KEY_X+8, 0},
	{0},
	{0},
	{0},
};

static struct tw_key_press cxc[MAX_KEY_SEQ_LEN] = {
	{KEY_X+8, MODIFIER_CTRL},
	{KEY_C+8, 0},
	{0},
	{0},
	{0},
};

static struct tw_key_press cxt[MAX_KEY_SEQ_LEN] = {
	{KEY_X+8, MODIFIER_CTRL},
	{KEY_T+8, 0},
	{0},
	{0},
	{0},
};

static struct tw_key_press ccu[MAX_KEY_SEQ_LEN] = {
	{KEY_C+8, MODIFIER_CTRL},
	{KEY_U+8, 0},
	{0},
	{0},
	{0},
};




int main(int argc, char *argv[])
{
	//this is the fake key

	struct tw_binding_node *node = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(node); //init as root
	tw_binding_add_key(node, NULL, cxx, print_key, 'x', NULL);
	tw_binding_add_key(node, NULL, cxc, print_key, 'c', NULL);
	tw_binding_add_key(node, NULL, cxt, print_key, 't', NULL);
	tw_binding_add_key(node, NULL, ccu, print_key, 'u', NULL);
	vtree_print(&node->node, print_tree_key, 0);
	//okay, let us
	vector_t v;
	vector_init(&v, sizeof(struct tw_press), NULL);
	vtree_iterate(&node->node, &v, cache_input);
	for (int i = 0; i < v.len; i++) {
		struct tw_press *press = vector_at(&v, i);
		fprintf(stderr, "%s-%d\n",  (press->modifier == MODIFIER_CTRL) ? "Ctrl" : "No",
			press->keycode-8);
	}

	vtree_destroy(&node->node, free);

	return 0;
}

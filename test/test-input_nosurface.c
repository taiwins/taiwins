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

#include "input.1.h"


void print_key(struct weston_keyboard *keyboard, uint32_t option, void *data)
{
	fprintf(stderr, "hey, look, we just pressed %c!\n", option);
}

void print_tree_key(const struct vtree_node *node)
{
	const struct tw_binding_node *binding =
		container_of(node, const struct tw_binding_node, node);
	fprintf(stderr, "%s-%c\n",  (binding->modifier == MODIFIER_CTRL) ? "Ctrl" : "No",
		binding->keysym);
}


int main(int argc, char *argv[])
{
	struct tw_key_press cxx[MAX_KEY_SEQ_LEN] = {
		{XKB_KEY_x, MODIFIER_CTRL},
		{XKB_KEY_x, 0},
		{0},
		{0},
		{0},
	};

	struct tw_key_press cxc[MAX_KEY_SEQ_LEN] = {
		{XKB_KEY_x, MODIFIER_CTRL},
		{XKB_KEY_c, 0},
		{0},
		{0},
		{0},
	};

	struct tw_key_press cxt[MAX_KEY_SEQ_LEN] = {
		{XKB_KEY_x, MODIFIER_CTRL},
		{XKB_KEY_t, 0},
		{0},
		{0},
		{0},
	};

	struct tw_key_press ccu[MAX_KEY_SEQ_LEN] = {
		{XKB_KEY_c, MODIFIER_CTRL},
		{XKB_KEY_u, 0},
		{0},
		{0},
		{0},
	};

	struct tw_binding_node *node = xmalloc(sizeof(struct tw_binding_node));
	tw_binding_node_init(node); //init as root
	tw_binding_add_key(node, NULL, cxx, print_key, 'x', NULL);
	tw_binding_add_key(node, NULL, cxc, print_key, 'c', NULL);
	tw_binding_add_key(node, NULL, cxt, print_key, 't', NULL);
	tw_binding_add_key(node, NULL, ccu, print_key, 'u', NULL);
	vtree_print(&node->node, print_tree_key, 0);
	vtree_destroy(&node->node, free);
	return 0;
}

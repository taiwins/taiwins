#include <xkbcommon/xkbcommon.h>
#include <sequential.h>
#include <tree.h>

/**
 * thanks to the knowledge by archwiki, the xkbcommon is the library that does
 * all the mapping, it has
 * - model:"pc105, pc104, cheryblue, emachines...",
 * - layouts: "languages like us, fr",
 * - variants: "dvorak" keyboard for us layouts, anyway, people always wanna new
     different keyboard.
 * - options: the one I use is 'switch lctrl-lalt'
 *
 * you can generate the keyboard.conf use setxkbmap
 */
struct tw_keypress {
	list_t command;
	xkb_keysym_t keysym;
};
#define list2keypress(ptr)	container_of(ptr, struct tw_keypress, command)

//at the layer, you only need to know one key
struct tw_keymap_tree {
	//struct list_t *keyseq;
	xkb_keysym_t keysym;
	vtree_node node;
};
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

//major job. translate key sequences into the tree
void
update_tw_keymap_tree(struct tw_keymap_tree *root, struct list_t *keyseq)
{
	struct tw_keypress *ks = list2keypress(keyseq);
	list_for_each(keysym, ks, command) {

	}
}


//test the translation between xkbcommon and linux scan code
/*
int main(int argc, char *argv[])
{

	return 0;
}
*/

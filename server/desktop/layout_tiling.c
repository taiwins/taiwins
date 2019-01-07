#include <stdlib.h>
#include <helpers.h>
#include <sequential.h>
#include <tree.h>
#include "layout.h"


static void
emplace_noop(const enum layout_command command, const struct layout_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct layout_op *ops)
{
	ops[0].end = true;
}

void
emplace_tiling(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops);


////////////////////////////////////////////////////////////////////////////////
////////////////////////////// tining layout ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

struct tiling_view {
	//vertical split or horizental split
	bool vertical;
	float portion;
	//you can check empty by view or check the size of the node
	struct weston_view *v;
	struct vtree_node node;
};

struct tiling_output {
	//if o is empty, we don't have any outputs
	struct weston_output *o;
	struct tiling_view *root;
	//the gap is here to determine the size between views
	uint32_t gap;
};

void
tiling_layout_init(struct layout *l, struct weston_layer *ly)
{
	layout_init(l, ly);
	l->user_data = xmalloc(sizeof(vector_t));
	l->command = emplace_tiling;
	//change this, now you have to consider the option that someone unplug
	//the monitor
	vector_init(l->user_data, sizeof(struct vtree_node *), NULL);
}

void
tiling_layout_end(struct layout *l)
{
	layout_release(l);
	vector_destroy(l->user_data);
	free(l->user_data);
}


void tiling_add_output(struct layout *l, struct weston_output *o)
{
}

void tiling_rm_output(struct layout *l, struct weston_output *o)
{

}

static inline struct tiling_output *
tiling_output_find(struct layout *l, struct weston_output *wo)
{
	vector_t *v = l->user_data;
	for (int i = 0; i < v->len; i++) {
		struct tiling_output *o = vector_at(v, i);
		if (o->o == wo)
			return o;
	}
	return NULL;
}

static int
cmp_views(const void *v, const struct vtree_node *n)
{
	const struct tiling_view *tv =
		container_of(n, const struct tiling_view, node);
	return (v == tv->v) ? 0 : -1;
}

static struct tiling_view *
tiling_view_find(struct tiling_view *root, struct weston_view *v)
{
	struct vtree_node *tnode = vtree_search(&root->node, v,
						cmp_views);
	return container_of(tnode, struct tiling_view, node);
}

static struct tiling_view *
tiling_new_view(struct weston_view *v, bool vertical, float portion)
{
	struct tiling_view *tv = xmalloc(sizeof(struct tiling_view));
	vtree_node_init(&tv->node,
			offsetof(struct tiling_view, node));
	tv->vertical = vertical;
	tv->v = v;
	tv->portion = portion;
}


static void
tiling_destroy_view(struct tiling_view *v)
{
}

void
tilting_arrange_subtree(struct tiling_view *subtree)
{

}

static void
tiling_add(const enum layout_command command, const struct layout_op *arg,
	   struct weston_view *v, struct layout *l,
	   struct layout_op *ops)
{
	//you need to get the last focus views
	struct weston_layer *layer = l->layer;
	//the one on the top is obviously the
	struct weston_view *focused_view =
		container_of(layer->view_list.link.next,
			     struct weston_view, layer_link.link);
	struct tiling_output *to =
		tiling_output_find(l, focused_view->output);
	struct tiling_view *tv =
		tiling_view_find(to->root, focused_view);
	struct tiling_view *pv =
		container_of(tv->node.parent, struct tiling_view, node);

	double new_portion = 1.0 - (double)pv->node.children.len /
		(double)pv->node.children.len+1;
	double rest_portion = 1.0 - new_portion;

	struct tiling_view *new_view = tiling_new_view(v, false, new_portion);

	for (int i = 0; i < pv->node.children.len; i++) {
		struct tiling_view *sv = container_of(
			*(struct vtree_node **)vector_at(&pv->node.children, i),
			struct tiling_view,
			node);
		sv->portion *= rest_portion;
	}
	//now officially adding the node to the children, remember, we add it to
	//the end
	vtree_node_add_child(&pv->node, &new_view->node);

}


void
emplace_tiling(const enum layout_command command, const struct layout_op *arg,
	       struct weston_view *v, struct layout *l,
	       struct layout_op *ops)
{
	//make a dummy one now
	struct placement_node {
		enum layout_command command;
		layout_fun_t fun;
	};

	static struct placement_node t_ops[] = {
		{DPSR_focus, emplace_noop},
		{DPSR_add, emplace_noop},
		{DPSR_del, emplace_noop},
		{DPSR_deplace, emplace_noop},
		{DPSR_up, emplace_noop},
		{DPSR_down, emplace_noop},
		{DPSR_left, emplace_noop},
		{DPSR_right, emplace_noop},
		{DPSR_resize, emplace_noop},
	};
	assert(t_ops[command].command == command);
	t_ops[command].fun(command, arg, v, l, ops);
}

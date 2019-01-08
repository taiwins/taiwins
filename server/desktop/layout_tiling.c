#include <string.h>
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

struct tiling_output;
struct tiling_view {
	//vertical split or horizental split
	bool vertical;
	float portion;
	//you can check empty by view or check the size of the node
	struct weston_view *v;
	struct tiling_output *o;
	struct vtree_node node;
	//we have 32 layers of split
	int8_t coding[32];
	int8_t level;
};


struct tiling_output {
	//if o is empty, we don't have any outputs
	struct weston_output *o;
	struct tiling_view *root;
	//the gap is here to determine the size between views
	uint32_t inner_gap;
	uint32_t outer_gap;
};

struct tiling_user_data {
	vector_t outputs;
	//the floating layout which is on
	struct layout *floating;
};

void
tiling_layout_init(struct layout *l, struct weston_layer *ly, struct layout *floating)
{
	layout_init(l, ly);

	l->user_data = xmalloc(sizeof(struct tiling_user_data));
	struct tiling_user_data *user_data =  l->user_data;
	user_data->floating = floating;

	l->command = emplace_tiling;
	//change this, now you have to consider the option that someone unplug
	//the monitor
	vector_init(&user_data->outputs, sizeof(struct vtree_node *), NULL);
}

void
tiling_layout_end(struct layout *l)
{
	struct tiling_user_data *user_data =  l->user_data;

	layout_release(l);
	//okay, we need to destroy all the views explicitly
	for (int i = 0; i < user_data->outputs.len; i++) {
		struct tiling_output *output = vector_at(&user_data->outputs, i);
		//change this, we need to destroy every single views
		vtree_destroy(&output->root->node, NULL);
	}
	vector_destroy(&user_data->outputs);
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
	struct tiling_user_data *user_data = l->user_data;
	vector_t *v = &user_data->outputs;
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

static inline struct tiling_view *
tiling_new_view(struct weston_view *v)
{
	struct tiling_view *tv = xmalloc(sizeof(struct tiling_view));
	vtree_node_init(&tv->node,
			offsetof(struct tiling_view, node));
	tv->v = v;
	return tv;
}

static void
tiling_view_insert(struct tiling_view *parent, struct tiling_view *tv, off_t offset)
{
	tv->level = parent->level+1;
	//we just make the assert here
	assert(tv->level <= 31);
	memcpy(tv->coding, parent->coding, sizeof(int8_t) * 32);
	tv->coding[tv->level] = offset;
	tv->o = parent->o;
	tv->vertical = parent->vertical;
	//now officially adding the node to the children, remember, we add it to
	vtree_node_insert(&parent->node, &tv->node, offset);
	//now you need to find the space for pv
}

static inline void
tiling_destroy_view(struct tiling_view *v)
{
	free(v);
}

static struct weston_geometry
tiling_subtree_space(struct tiling_view *v,
		     struct tiling_view *root,
		     struct weston_output *o)
{
	struct tiling_view *subtree = root;
	struct weston_geometry geo = {
		o->x, o->y, o->width, o->height,
	};
	for (int i = 0; i < v->level+1; i++) {
		struct vtree_node *node =
			vtree_ith_child(&subtree->node,
					v->coding[i]);
		float leading = 0.0;
		for (int j = 0; j < v->coding[i]; j++)
			leading += container_of(vtree_ith_child(&subtree->node, j),
						struct tiling_view, node)->portion;
		struct tiling_view *n = container_of(node, struct tiling_view, node);
		if (subtree->vertical) {
			geo.y = geo.y + geo.height * leading;
			geo.height = n->portion * geo.height;
		} else {
			geo.x = geo.x + geo.width * leading;
			geo.width = n->portion * geo.width;
		}
		subtree = n;
	}
	return geo;
}

static int
tilting_arrange_subtree(struct tiling_view *subtree, struct weston_geometry *geo,
			struct layout_op *data_out)
{
	//leaf
	if (subtree->v) {
		data_out->v = subtree->v;
		data_out->pos.x = geo->x + ((subtree->vertical) ?
					    subtree->o->outer_gap :
					    subtree->o->inner_gap);
		data_out->pos.y = geo->y + ((subtree->vertical) ?
					    subtree->o->inner_gap :
					    subtree->o->outer_gap);
		data_out->size.width =
			geo->width - ((subtree->vertical) ?
				      subtree->o->outer_gap :
				      subtree->o->inner_gap);
		data_out->size.height =
			geo->height - ((subtree->vertical) ?
				       subtree->o->inner_gap :
				       subtree->o->outer_gap);
		data_out->end = false;
		return 1;
	}
	//internal node
	int count = 0;
	float leading = 0.0;
	for (int i = 0; i < subtree->node.children.len; i++) {
		struct vtree_node *node =
			vtree_ith_child(&subtree->node, i);
		struct tiling_view *n = container_of(node, struct tiling_view, node);

		struct weston_geometry sub_space = *geo;
		sub_space.x += (subtree->vertical) ? 0 : leading * geo->width;
		sub_space.width = (subtree->vertical) ? geo->width : n->portion * geo->width;

		sub_space.y += (subtree->vertical) ? leading * geo->height : 0;
		sub_space.height = (subtree->vertical) ? n->portion * geo->height : geo->height;
		leading += n->portion;
		count += tilting_arrange_subtree(n, &sub_space, &data_out[count]);
	}
	return count;
}

static struct tiling_view*
tiling_find_launch_point(struct layout *l)
{
	struct weston_layer *layer = l->layer;
	struct tiling_user_data *user_data = l->user_data;
	struct weston_layer *floating_layer = user_data->floating->layer;
	//try to get the launch node
	struct tiling_view *pv = NULL;
	//if the layout is not empty.
	struct tiling_view *tv;
	if (wl_list_length(&layer->view_list.link) > 0) {
		struct weston_view *focused_view =
			container_of(layer->view_list.link.next, struct weston_view,
				     layer_link.link);
		struct tiling_output *to =
			tiling_output_find(l, focused_view->output);
		tv = tiling_view_find(to->root, focused_view);
	} else if (wl_list_length(&floating_layer->view_list.link) > 0) {
		struct weston_view *focused_view =
			container_of(layer->view_list.link.next, struct weston_view,
				     layer_link.link);
		struct tiling_output *to =
			tiling_output_find(l, focused_view->output);
		tv = to->root;
	} else {
		struct tiling_output *to = vector_at(&user_data->outputs, 0);
		tv = to->root;
	}
	//test if tv is root node
	pv = (tv->node.parent) ?
		container_of(tv->node.parent, struct tiling_view, node) : tv;
	//test if tv is too deep, we shouldn't do it here, it should be in the split
	pv = (pv->level >= 31) ?
		container_of(pv->node.parent, struct tiling_view, node) : pv;

	return pv;
}

/**
 * we need this and we need a additional split call
 */
static void
tiling_add(const enum layout_command command, const struct layout_op *arg,
	   struct weston_view *v, struct layout *l,
	   struct layout_op *ops)
{
	//find launch point
	struct tiling_view *pv = tiling_find_launch_point(l);
	struct tiling_view *root = pv->o->root;

	double occupied = 1.0 - (double)pv->node.children.len /
		(double)pv->node.children.len+1;
	double occupied_rest = 1.0 - occupied;
	//creating new view here
	struct tiling_view *new_view = tiling_new_view(v);
	new_view->portion = occupied;

	for (int i = 0; i < pv->node.children.len; i++) {
		struct tiling_view *sv = container_of(
			*(struct vtree_node **)vector_at(&pv->node.children, i),
			struct tiling_view,
			node);
		sv->portion *= occupied_rest;
	}
	tiling_view_insert(pv, new_view, 0);
	struct weston_geometry space =
		tiling_subtree_space(new_view, root, pv->o->o);
	int count = tilting_arrange_subtree(pv, &space, ops);
	ops[count].end = true;
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
		{DPSR_add, tiling_add},
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

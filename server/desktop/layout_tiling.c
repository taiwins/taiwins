/*
 * layout_tiling.c - taiwins desktop tiling layout implementation
 *
 * Copyright (c) 2019 Xichen Zhou
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

#include <string.h>
#include <stdlib.h>
#include <helpers.h>
#include <sequential.h>
#include <tree.h>
#include "workspace.h"
#include "layout.h"

static void
emplace_noop(UNUSED_ARG(const enum layout_command command),
             UNUSED_ARG(const struct layout_op *arg),
             UNUSED_ARG(struct weston_view *v), UNUSED_ARG(struct layout *l),
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
	//the interval is updated when inserting/deleting/resizing
	float interval[2];
	//you can check empty by view or check the size of the node
	struct weston_view *v;
	struct weston_output *output;
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
	struct weston_geometry curr_geo;
};

struct tiling_user_data {
	vector_t outputs;
	//the floating layout which is on
	struct layout *floating;
};

static inline struct tiling_view *
tiling_new_view(struct weston_view *v)
{
	struct tiling_view *tv = zalloc(sizeof(struct tiling_view));
	vtree_node_init(&tv->node,
			offsetof(struct tiling_view, node));
	tv->v = v;
	return tv;
}

static inline void
tiling_free_view(struct tiling_view *v)
{
	free(v);
}

static void
_free_tiling_output_view(void *data)
{
	struct tiling_view *view = data;
	if (view->v)
		weston_desktop_surface_unlink_view(view->v);
	free(data);
}

static void
_free_tiling_output(void *data)
{
	struct tiling_output *output = data;
	vtree_destroy(&output->root->node, _free_tiling_output_view);
}

void
tiling_layout_init(struct layout *l, struct weston_layer *ly,
                   struct layout *floating)
{
	layout_init(l, ly);

	l->user_data = zalloc(sizeof(struct tiling_user_data));
	struct tiling_user_data *user_data =  l->user_data;
	user_data->floating = floating;

	l->command = emplace_tiling;
	vector_init(&user_data->outputs,
		    sizeof(struct tiling_output),
		    _free_tiling_output);
}

void
tiling_layout_end(struct layout *l)
{
	struct tiling_user_data *user_data =  l->user_data;
	layout_release(l);
	vector_destroy(&user_data->outputs);
	free(user_data);
}

/****************************************************************
 * tiling output
 ***************************************************************/
void
tiling_add_output(struct layout *l, struct tw_output *o)
{
	struct tiling_user_data *user_data =  l->user_data;
	struct tiling_output output;
	//setup the data
	output.o = o->output;
	output.inner_gap = o->inner_gap;
	output.outer_gap = o->outer_gap;
	output.curr_geo = o->desktop_area;
	//setup the first node
	output.root = tiling_new_view(NULL);
	output.root->level = 0;
	output.root->output = o->output;
	output.root->portion = 1.0;
	output.root->vertical = false;

	vector_append(&user_data->outputs, &output);
}

void
tiling_rm_output(struct layout *l, struct weston_output *o)
{
	struct tiling_user_data *user_data =  l->user_data;

	for (int i = 0; i < user_data->outputs.len; i++) {
		struct tiling_output *to = vector_at(&user_data->outputs, i);
		if (to->o == o)
			vector_erase(&user_data->outputs, i);
	}
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

void
tiling_resize_output(struct layout *l, struct tw_output *o)
{
	struct tiling_output *output = tiling_output_find(l, o->output);
	output->curr_geo = o->desktop_area;
	output->inner_gap = o->inner_gap;
	output->outer_gap = o->outer_gap;
}

/**************************************************************
 * tiling view tree operations
 *************************************************************/
static inline struct tiling_view *
tiling_view_ith_node(struct tiling_view *parent, off_t index)
{
	return container_of(
		vtree_ith_child(&parent->node, index), struct tiling_view,
		node);
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

static inline struct weston_geometry
tiling_space_divide(const struct weston_geometry *space, float start, float end,
                    bool vertical)
{
	struct weston_geometry geo = {0};

	if (vertical) {
		geo.x = space->x;
		geo.width = space->width;
		geo.y = space->y + start * space->height;
		geo.height = space->height * (end - start);
	} else {
		geo.x = space->x + geo.width * start;
		geo.width = space->width * (end-start);
		geo.y = space->y;
		geo.height = space->height;
	}

	return geo;
}

//update based on portion
static inline void
tiling_update_children(struct tiling_view *parent)
{
	float leading = 0.0;
	for (int i = 0; i < parent->node.children.len; i++) {
		struct tiling_view *sv = tiling_view_ith_node(parent, i);
		sv->coding[sv->level-1] = i;
		sv->interval[0] = leading;
		sv->interval[1] = (i == (parent->node.children.len-1)) ? 1.0 :
			leading + sv->portion;
		sv->vertical = (sv->v) ? parent->vertical :
			sv->vertical;
		leading += sv->portion;
	}
}

/**
 * @brief the ONE interface to test whether it is possible to make change in
 * subtree
 */
static inline bool
is_subtree_valid(const struct weston_geometry *space, const bool vertical,
		 const float portions[], const size_t len,
		 const struct tiling_output *constrain)
{
	//first lets
	float outer_size = (vertical) ? space->width : space->height;
	float inner_size = (vertical) ? space->height : space->width;
	float min_portion = (constrain->inner_gap * 2.0) / inner_size;

	if (constrain->outer_gap * 2 >= outer_size)
		return false;
	for (unsigned i = 0; i < len; i++)
		if (portions[i] < min_portion)
			return false;
	return true;
}

/* it could failed to insert the view */
static bool
tiling_view_insert(struct tiling_view *parent,
                   struct tiling_view *tv, off_t offset,
                   const struct weston_geometry *parent_geo,
                   const struct tiling_output *output)
{
	double occupied = 1.0 - (double)parent->node.children.len /
		((double)parent->node.children.len+1);
	double occupied_rest = 1.0 - occupied;
	{
		//test possibility of insert
		size_t len = parent->node.children.len;
		float portions[len+1];
		for (unsigned i = 0; i < len; i++) {
			struct tiling_view *sv =
				tiling_view_ith_node(parent, i);
			portions[i] = sv->portion * occupied_rest;
		}
		portions[len] = occupied;
		if (!is_subtree_valid(parent_geo, parent->vertical,
				      portions, len+1, output))
			return false;

	}
	//re-assign all the portions
	for (int i = 0; i < parent->node.children.len; i++) {
		struct tiling_view *sv = tiling_view_ith_node(parent, i);
		sv->portion *= occupied_rest;
	}
	tv->portion = occupied;
	tv->level = parent->level+1;
	//assert just in case
	assert(tv->level <= 31);
	memcpy(tv->coding, parent->coding, sizeof(int8_t) * 32);
	tv->coding[tv->level-1] = offset;
	tv->output = parent->output;
	tv->vertical = parent->vertical;
	vtree_node_insert(&parent->node, &tv->node, offset);

	//update the subtree
	tiling_update_children(parent);
	return true;
}

static struct tiling_view *
tiling_view_erase(struct tiling_view *view)
{
	//you cannot remove a non-leaf node
	if (view->node.children.len)
		return view;
	//try to get the portion here, I don't know if removing everything it is
	//a good idea
	double rest_occupied = 1.0 - view->portion;
	off_t index = view->coding[view->level-1];
	struct tiling_view *parent = view->node.parent ?
		container_of(view->node.parent, struct tiling_view, node) :
		NULL;
	vtree_node_remove(view->node.parent, index);
	tiling_free_view(view);
	if (!parent)
		return NULL;
	//updating children info
	float leading = 0.0;
	for (int i = 0; i < parent->node.children.len; i++) {
		struct tiling_view *sv = tiling_view_ith_node(parent, i);
		sv->coding[sv->level-1] = i;
		sv->portion /= rest_occupied;
		sv->interval[0] = leading;
		sv->interval[1] = (i == (parent->node.children.len-1)) ?
			1.0 : leading + sv->portion;
		leading += sv->portion;
	}
	//if the loop above was executed, This recursive code would not run.
	//otherwise, It means this parent is a empty node:
	// 1) it is not root (it has parent).
	// 2) It does not has children.
	// 3) it has no view. Then this parent is safe to remove
	if (parent && !parent->v &&
	    !parent->node.children.len && parent->node.parent)
		return tiling_view_erase(parent);
	return parent;
}

/**
 * /brief resize a view and populate the effect on subtree
 *
 * we try to have the most complex effect resizing effect here, depends on where
 * you have your have your cursor.
 */
static bool
tiling_view_resize(struct tiling_view *view,
                   float delta_head, float delta_tail,
                   const struct weston_geometry *parent_geo,
                   const struct tiling_output *output)
{
	struct tiling_view *parent = view->node.parent ?
		container_of(view->node.parent, struct tiling_view, node) :
		NULL;
	//I am the only node
	if (!parent || parent->node.children.len <= 1)
		return false;
	//deal with delta_tail, delta_head
	if (view->coding[view->level-1] == parent->node.children.len-1)
		delta_tail = 0.0;
	if (view->coding[view->level-1] == 0)
		delta_head = 0.0;
	if (delta_head == 0.0 && delta_tail == 0.0)
		return false;

	//get new portions
	float portions[parent->node.children.len];
	int index = view->coding[view->level-1];
	//space left for left views
	float occupied_rest = view->interval[0] + delta_head;
	for (int i = 0; i < index; i++) {
		struct tiling_view *tv = tiling_view_ith_node(parent, i);
		portions[i] = tv->portion * (occupied_rest /view->interval[0]);
	}
	//space for view
	portions[index] = view->portion - delta_head + delta_tail;
	//space left for right views
	float occupied_orig = 1.0 - view->interval[1];
	occupied_rest = 1 - (view->interval[1] + delta_tail);
	for (int i = index+1; i < parent->node.children.len; i++) {
		struct tiling_view *tv = tiling_view_ith_node(parent, i);
		portions[i] = tv->portion * (occupied_rest / occupied_orig);
	}
	if (!is_subtree_valid(parent_geo, parent->vertical, portions,
			      parent->node.children.len, output))
		return false;

	for (int i = 0; i < parent->node.children.len; i++) {
		struct tiling_view *tv = tiling_view_ith_node(parent, i);
		tv->portion = portions[i];
	}
	tiling_update_children(parent);
	return true;
}

/**
 * /brief shift a view in its parent list
 *
 */
static inline void
tiling_view_shift(struct tiling_view *view, bool forward)
{
	vtree_node_shift(&view->node, forward);
	if (view->node.parent)
		tiling_update_children((struct tiling_view *)
		                       vtree_container(view->node.parent));
}

/**
 * /brief dividing a space of subtree from its parent
 */
static struct weston_geometry
tiling_subtree_space(struct tiling_view *v,
                     struct tiling_view *root,
                     const struct weston_geometry *space)
{
	struct tiling_view *subtree = root;
	struct weston_geometry geo = *space;

	//from 0 to level - 1
	for (int i = root->level; i < v->level; i++) {
		struct vtree_node *node =
			vtree_ith_child(&subtree->node,
					v->coding[i]);
		struct tiling_view *n =
			container_of(node, struct tiling_view, node);
		if (subtree->vertical) {
			geo.y = geo.y + geo.height * n->interval[0];
			geo.height = n->portion * geo.height;
		} else {
			geo.x = geo.x + geo.width * n->interval[0];
			geo.width = n->portion * geo.width;
		}
		subtree = n;
	}
	return geo;
}

static int
tiling_arrange_subtree(struct tiling_view *subtree, struct weston_geometry *geo,
                       struct layout_op *data_out,
                       const struct tiling_output *o)
{
	//leaf
	if (subtree->v) {
		data_out->v = subtree->v;
		data_out->pos.x = geo->x + ((subtree->vertical) ?
					    o->outer_gap :
					    o->inner_gap);
		data_out->pos.y = geo->y + ((subtree->vertical) ?
					    o->inner_gap :
					    o->outer_gap);
		data_out->size.width =
			geo->width - 2 * ((subtree->vertical) ?
					  o->outer_gap :
					  o->inner_gap);
		data_out->size.height =
			geo->height - 2 * ((subtree->vertical) ?
					   o->inner_gap :
					   o->outer_gap);
		data_out->end = false;
		return 1;
	}
	//internal node
	int count = 0;
	for (int i = 0; i < subtree->node.children.len; i++) {
		struct vtree_node *node =
			vtree_ith_child(&subtree->node, i);
		struct tiling_view *n = container_of(node, struct tiling_view, node);

		struct weston_geometry sub_space = *geo;
		float portion = n->interval[1] - n->interval[0];
		sub_space.x += (subtree->vertical) ? 0 : n->interval[0] * geo->width;
		sub_space.width = (subtree->vertical) ? geo->width : portion * geo->width;
		sub_space.y += (subtree->vertical) ? n->interval[0] * geo->height : 0;
		sub_space.height = (subtree->vertical) ? portion * geo->height : geo->height;

		count += tiling_arrange_subtree(n, &sub_space, &data_out[count], o);
	}
	return count;
}

static inline struct tiling_view *
tiling_focused_view(struct layout *l)
{
	struct weston_view *focused_view =
		container_of(l->layer->view_list.link.next, struct weston_view,
			     layer_link.link);
	return tiling_view_find(
		tiling_output_find(l, focused_view->output)->root,
		focused_view);
}


/* the launch point is based on last focused view */
static struct tiling_view*
tiling_find_launch_point(struct layout *l, struct tiling_output *to)
{
	//parent view, focused view
	struct tiling_view *pv, *fv =
		wl_list_length(&l->layer->view_list.link) > 0 ?
		tiling_focused_view(l) : to->root;
	//test if tv is root node
	pv = (fv->node.parent) ?
		container_of(fv->node.parent, struct tiling_view, node) : fv;
	//test if tv is too deep.
	pv = (pv->level >= 31) ?
		container_of(pv->node.parent, struct tiling_view, node) : pv;
	return pv;
}


/*****************************************************************
 * tiling apis
 ****************************************************************/
static void
tiling_add(UNUSED_ARG(const enum layout_command command),
           UNUSED_ARG(const struct layout_op *arg),
	   struct weston_view *v, struct layout *l,
	   struct layout_op *ops)
{
	//insert view based on lasted focused view
	struct tiling_output *to = tiling_output_find(l, v->output);
	//TODO remove this hack: because v is already in the layer link, we need
	//to temporarily remove it to get the correct result
	weston_layer_entry_remove(&v->layer_link);
	struct tiling_view *pv = tiling_find_launch_point(l, to);
	weston_layer_entry_insert(&l->layer->view_list, &v->layer_link);
	struct tiling_output *tiling_output = tiling_output_find(l, pv->output);
	struct tiling_view *root = tiling_output->root;
	struct weston_geometry space =
		tiling_subtree_space(pv, root, &tiling_output->curr_geo);

	struct tiling_view *new_view = tiling_new_view(v);
	//we could fail to insert
	if (tiling_view_insert(pv, new_view, 0,
			       &space, tiling_output)) {
		int count = tiling_arrange_subtree(pv, &space,
		                                   ops, tiling_output);
		ops[count].end = true;
	} else {
		tiling_free_view(new_view);
		ops[0].end = true;
	}
}

static void
tiling_del(UNUSED_ARG(const enum layout_command command),
           UNUSED_ARG(const struct layout_op *arg),
	   struct weston_view *v, struct layout *l,
	   struct layout_op *ops)
{
	struct tiling_output *tiling_output = tiling_output_find(l, v->output);
	struct tiling_view *view = tiling_view_find(tiling_output->root, v);
	struct tiling_view *parent = tiling_view_erase(view);
	if (parent) {
		struct weston_geometry space =
			tiling_subtree_space(parent, tiling_output->root,
					     &tiling_output->curr_geo);
		int count = tiling_arrange_subtree(parent, &space,
		                                   ops, tiling_output);
		ops[count].end = true;
	} else
		ops[0].end = true;

}

static void
_tiling_resize(const struct layout_op *arg, struct tiling_view *view,
	       struct layout *l, struct layout_op *ops, bool force_update)
{
	struct tiling_output *tiling_output =
		tiling_output_find(l, view->output);
	struct tiling_view *parent = container_of(view->node.parent,
						  struct tiling_view, node);
	struct weston_geometry space =
		tiling_subtree_space(parent, tiling_output->root,
				     &tiling_output->curr_geo);
	struct weston_geometry view_space =
		tiling_subtree_space(view, parent, &space);
	//get the ratio from global coordinates
	double rx = wl_fixed_to_double(arg->sx) / view_space.width;
	double ry = wl_fixed_to_double(arg->sy) / view_space.height;
	//deal with current parent.
	float ph = 0.0, pt = 0.0;
	if (parent->vertical) {
		ph = (ry) <= 0.5 ? arg->dy / space.height : 0.0;
		pt = (ry) >  0.5 ? arg->dy / space.height : 0.0;
	} else {
		ph = (rx) <= 0.5 ? arg->dx / space.width : 0.0;
		pt = (rx) >  0.5 ? arg->dx / space.width : 0.0;
	}
	bool resized = tiling_view_resize(view, ph, pt, &space,
					  tiling_output);
	//try to resize the parent->parent,
	struct tiling_view *gparent = (parent->node.parent) ?
		container_of(parent->node.parent, struct tiling_view, node) :
		NULL;
	ops[0].end = true;
	if (gparent) {
		_tiling_resize(arg, parent, l, ops, resized || force_update);
	} else if (resized || force_update) {
		int count = tiling_arrange_subtree(parent, &space,
		                                   ops, tiling_output);
		ops[count].end = true;
	}
}

static void
tiling_resize(const enum layout_command command, const struct layout_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct layout_op *ops)
{
	struct tiling_output *tiling_output = tiling_output_find(l, v->output);
	struct tiling_view *view = tiling_view_find(tiling_output->root, v);
	_tiling_resize(arg, view, l, ops, false);
}

static void
tiling_update(UNUSED_ARG(const enum layout_command command),
              UNUSED_ARG(const struct layout_op *arg),
              UNUSED_ARG(struct weston_view *v),
              struct layout *l, struct layout_op *ops)
{
	struct tiling_output *tiling_output =
		tiling_output_find(l, (struct weston_output *)arg->o);

	int count = tiling_arrange_subtree(tiling_output->root, &
	                                   tiling_output->curr_geo,
	                                   ops, tiling_output);
	ops[count].end = true;
}

/**
 * do a vertical/horizental split
 */
static void
_tiling_split(UNUSED_ARG(const enum layout_command command),
              UNUSED_ARG(const struct layout_op *arg),
              struct weston_view *v, struct layout *l, bool vertical,
              struct layout_op *ops)
{
	struct tiling_output *tiling_output = tiling_output_find(l, v->output);
	struct tiling_view *view = tiling_view_find(tiling_output->root, v);
	struct tiling_view *parent = container_of(view->node.parent,
						  struct tiling_view, node);

	//test if the view is the only child. So we do not need to split
	if (parent->node.children.len <= 1) {
		parent->vertical = vertical;
		ops[0].end = true;
		return;
	}

	struct weston_geometry space =
		tiling_subtree_space(view, tiling_output->root,
				     &tiling_output->curr_geo);
	struct tiling_view *new_view = tiling_new_view(v);
	view->v = NULL;
	view->vertical = vertical;
	tiling_view_insert(view, new_view, 0, &space, tiling_output);
	int count = tiling_arrange_subtree(view, &space, ops, tiling_output);
	ops[count].end = true;
}

static void
tiling_vsplit(const enum layout_command command, const struct layout_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct layout_op *ops)
{
	_tiling_split(command, arg, v, l, true, ops);
}

static void
tiling_hsplit(const enum layout_command command, const struct layout_op *arg,
	      struct weston_view *v, struct layout *l,
	      struct layout_op *ops)
{
	_tiling_split(command, arg, v, l, false, ops);
}

static void
tiling_merge(UNUSED_ARG(const enum layout_command command),
             UNUSED_ARG(const struct layout_op *arg),
             UNUSED_ARG(struct weston_view *v), UNUSED_ARG(struct layout *l),
             struct layout_op *ops)
{
	//remove current view and then insert at grandparent list
	struct tiling_output *tiling_output = tiling_output_find(l, v->output);
	struct tiling_view *view = tiling_view_find(tiling_output->root, v);
	struct tiling_view *parent = container_of(view->node.parent,
						  struct tiling_view, node);
	struct tiling_view *gparent = parent->node.parent ?
		container_of(parent->node.parent, struct tiling_view, node) :
		NULL;
	//if we are not
	if (view->node.children.len || !gparent) {
		ops[0].end = true;
		return;
	}
	struct weston_geometry space =
		tiling_subtree_space(gparent, tiling_output->root,
				     &tiling_output->curr_geo);

	tiling_view_erase(view);
	view = tiling_new_view(v);
	//TODO deal with the case that it cannot insert
	tiling_view_insert(gparent, view, 0, &space, tiling_output);
	int count = tiling_arrange_subtree(gparent, &space, ops, tiling_output);
	ops[count].end = true;
}

/**
 * /brief toggle vertical
 */
static void
tiling_toggle(UNUSED_ARG(const enum layout_command command),
              UNUSED_ARG(const struct layout_op *arg),
              UNUSED_ARG(struct weston_view *v), UNUSED_ARG(struct layout *l),
              struct layout_op *ops)
{
	struct tiling_output *tiling_output = tiling_output_find(l, v->output);
	struct tiling_view *view = tiling_view_find(tiling_output->root, v);
	struct tiling_view *parent = container_of(view->node.parent,
						  struct tiling_view, node);
	struct weston_geometry space =
		tiling_subtree_space(parent, tiling_output->root,
				     &tiling_output->curr_geo);

	if (parent) {
		parent->vertical = !parent->vertical;
		int count = tiling_arrange_subtree(parent, &space,
		                                   ops, tiling_output);
		ops[count].end = true;
	}
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
		{DPSR_del, tiling_del},
		{DPSR_deplace, emplace_noop},
		{DPSR_toggle, tiling_toggle},
		{DPSR_resize, tiling_resize},
		{DPSR_vsplit, tiling_vsplit},
		{DPSR_hsplit, tiling_hsplit},
		{DPSR_merge, tiling_merge},
		{DPSR_output_resize, tiling_update},
	};
	assert(t_ops[command].command == command);
	t_ops[command].fun(command, arg, v, l, ops);
}

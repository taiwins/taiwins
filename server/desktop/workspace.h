#ifndef TW_WORKSPACE_H
#define TW_WORKSPACE_H

#include <stdbool.h>
#include <compositor.h>
#include "unistd.h"
#include "layout.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct workspace {
	struct layout floating_layout;
	struct layout tiling_layout;
	//workspace does not distinguish the outputs.
	//so when we `switch_workspace, all the output has to update.
	//The layouting algorithm may have to worry about output
	struct weston_layer hidden_layer;
	struct weston_layer tiling_layer;
	struct weston_layer floating_layer;
	struct weston_layer fullscreen_layer;

	//this list will be used in creating/deleting views. switch workspace,
	//switch views by key, click views will be horrible though. You have to
	//go through the list
	//what about a hashed link-list ? Will it be faster?
	struct wl_list recent_views;

	//the only tiling layer here will create the problem when we want to do
	//the stacking layout, for example. Only show two views.
};

struct focused_view {
	struct weston_view *ev;
	struct wl_list link;
};


extern size_t workspace_size;

void workspace_init(struct workspace *wp, struct weston_compositor *compositor);
void workspace_release(struct workspace *);
void workspace_switch(struct workspace *to, struct workspace *from,
		      struct weston_keyboard *keyboard);

void arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
				const enum layout_command command,
				const struct layout_op *arg);

bool is_view_on_workspace(const struct weston_view *v, const struct workspace *ws);
bool is_workspace_empty(const struct workspace *ws);


bool workspace_focus_view(struct workspace *ws, struct weston_view *v);
void workspace_add_view(struct workspace *w, struct weston_view *view);
bool workspace_move_view(struct workspace *w, struct weston_view *v,
				  const struct weston_position *pos);


void workspace_add_output(struct workspace *wp, struct weston_output *output);
void workspace_remove_output(struct workspace *w, struct weston_output *output);


#ifdef  __cplusplus
}
#endif



#endif

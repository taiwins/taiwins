#ifndef TW_WORKSPACE_H
#define TW_WORKSPACE_H

#include <stdbool.h>
#include <compositor.h>
#include "unistd.h"
#include "layout.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct workspace;
extern size_t workspace_size;

void workspace_init(struct workspace *wp, struct weston_compositor *compositor);
void workspace_release(struct workspace *);
void workspace_switch(struct workspace *to, struct workspace *from);
static inline void free_workspace(void *ws) {
	workspace_release((struct workspace *)ws);
}

void arrange_view_for_workspace(struct workspace *ws, struct weston_view *v,
				const enum disposer_command command,
				const struct disposer_op *arg);

bool is_view_on_workspace(const struct weston_view *v, const struct workspace *ws);

void workspace_add_view(struct workspace *w, struct weston_view *view);


bool workspace_focus_view(struct workspace *ws, struct weston_view *v);
void workspace_add_output(struct workspace *wp, struct weston_output *output);
void workspace_remove_output(struct workspace *w, struct weston_output *output);
bool workspace_move_floating_view(struct workspace *w, struct weston_view *v,
				  const struct weston_position *pos);


#ifdef  __cplusplus
}
#endif



#endif

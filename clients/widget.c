#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <wayland-util.h>
#include <os/file.h>
#include <lua.h>
#include <lauxlib.h>

#include <ui.h>
#include "widget.h"

static int
redraw_panel_for_file(struct tw_event *e, int fd)
{
	struct shell_widget *widget = e->data;
	struct app_event ae = {
		.type = TW_TIMER,
		.time = widget->ancre.wl_globals->inputs.millisec,
	};
	//you set it here it will never work
	widget->fd = fd;
	//panel gets redrawed for once we have a event
	widget->ancre.do_frame(&widget->ancre, &ae);
	//if somehow my fd changes, it means I no longer watch this fd anymore
	if (widget->fd != fd)
		return TW_EVENT_DEL;
	else
		return TW_EVENT_NOOP;
}

static int
redraw_panel_for_timer(struct tw_event *e, int fd)
{
	struct shell_widget *widget = e->data;
	struct app_event ae = {
		.type = TW_TIMER,
		.time = widget->ancre.wl_globals->inputs.millisec,
	};

	widget->fd = fd;
	widget->ancre.do_frame(&widget->ancre, &ae);
	//test if this is a one time event
	if (!(widget->interval).it_interval.tv_sec &&
	    !widget->interval.it_interval.tv_nsec)
		return TW_EVENT_DEL;
	else
		return TW_EVENT_NOOP;
}

static void
shell_widget_event_from_timer(struct shell_widget *widget, struct itimerspec *time,
			      struct tw_event_queue *event_queue)
{
	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel_for_timer,
	};
	tw_event_queue_add_timer(event_queue, time, &redraw_widget);
}


static void
shell_widget_event_from_file(struct shell_widget *widget, const char *path,
			     struct tw_event_queue *event_queue)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (!fd)
		return;
	//you don't need to set the fd here
	widget->fd = fd;
	//if mask is zero the client api will deal with a default flag
	uint32_t mask = 0;

	struct tw_event redraw_widget = {
		.data = widget,
		.cb = redraw_panel_for_file,
	};
	tw_event_queue_add_source(event_queue, fd, &redraw_widget, mask);
}


void
shell_widget_activate(struct shell_widget *widget, struct tw_event_queue *queue)
{
	if (widget->interval.it_value.tv_sec || widget->interval.it_value.tv_nsec)
		shell_widget_event_from_timer(widget, &widget->interval, queue);
	else if (widget->file_path)
		shell_widget_event_from_file(widget, widget->file_path, queue);
	else if (widget->path_find) {
		int len = widget->path_find(widget, NULL);
		if (len) {
			char path[len + 1];
			shell_widget_event_from_file(widget, path, queue);
		}
	}
}


/*******************************************************************************
 * LUA widget
 ******************************************************************************/
struct nk_love_context;
struct shell_widget_lua_runtime {
	lua_State *L;
	struct nk_love_context *runtime;
	char widgetcb[32];
	char anchorcb[32];
};

static int
lua_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	struct shell_widget_lua_runtime *lua_runtime =
		widget->user_data;
	lua_State *L = lua_runtime->L;
	lua_getfield(L, LUA_REGISTRYINDEX, lua_runtime->anchorcb);
	//setup context
	lua_pcall(L, 1, 1, 0);
}

static void
lua_widget_cb(struct nk_context *ctx, float width, float height,
	      struct app_surface *app)
{
	//get widget
	struct shell_widget *widget =
		container_of(app, struct shell_widget, widget);
	struct shell_widget_lua_runtime *lua_runtime =
		widget->user_data;

	lua_State *L = lua_runtime->L;
	//fuction
	lua_getfield(L, LUA_REGISTRYINDEX, lua_runtime->widgetcb);
	//ui
	lua_getfield(L, LUA_REGISTRYINDEX, "runtime");

	if (lua_pcall(L, 1, 0, 0)) {
		const char *error = lua_tostring(L, -1);
		//if any error occured, we draw this instead
		nk_clear(ctx);
		nk_layout_row_dynamic(ctx, 20, 1);
		nk_text_colored(ctx, error, strlen(error), NK_TEXT_CENTERED,
				nk_rgb(255, 0, 0));
	}
}



/*******************************************************************************
 * sample widget
 ******************************************************************************/

static int
whatup_ancre(struct shell_widget *widget, struct shell_widget_label *label)
{
	strcpy(label->label, "what-up!");
	return 8;
}

struct shell_widget what_up_widget = {
	.ancre_cb = whatup_ancre,
	.draw_cb = NULL,
	.w = 200,
	.h = 150,
	.path_find = NULL,
	.interval = {{0},{0}},
	.file_path = NULL,
};

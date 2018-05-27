#include <assert.h>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include "client.h"
#include "shellui.h"
////////////////////////////////////////////////////////////////////////
/////////////////////////////////Widgets////////////////////////////////
////////////////////////////////////////////////////////////////////////

void
shell_widget_init_with_funcs(struct shell_widget *app,
		       void (*update_icon)(struct eglapp_icon *),
		       void (*draw_widget)(struct shell_widget *))
{
	struct eglapp_icon *icon = &app->icon;
	//this is a very awkward part, because we have to run the icon for the
	//first time to figure out the size it occupies
	update_icon(&app->icon);
	unsigned int icon_width = cairo_image_surface_get_width(icon->isurf);
	unsigned int icon_right = icon->box.w;
	icon->box = (struct bbox) {
		.x = icon_right - icon_width,
		.y = 0,
		.w = icon_width,
		.h = icon->box.h,
	};

	app->width = 200;
	app->height = 200;
	//callbacks
	app->icon.update_icon = update_icon;
	app->draw_widget = draw_widget;
}

void
shell_widget_init_with_script(struct shell_widget *app,
	const char *script)
{
//	struct shell_panel *panel = app->panel;
	int status = 0;

	app->L = luaL_newstate();
	luaL_openlibs(app->L);
	status += luaL_loadfile(app->L, script);
	status += lua_pcall(app->L, 0, 0, 0);
	if (status)
		return;

	app->width = 200;
	app->height = 200;
	//register globals
	void *ptr = lua_newuserdata(app->L, sizeof(void *));
	lua_setglobal(app->L, "application");
	//TODO, we should call update icon for the first time
}

static void
shell_widget_destroy(struct shell_widget *app)
{
	cairo_destroy(app->icon.ctxt);
	cairo_surface_destroy(app->icon.isurf);
}


static void
_free_widget(void *app)
{
	shell_widget_destroy((struct shell_widget *)app);
}

////////////////////////////////////////////////////////////////////////
//////////////////////////////////Panel/////////////////////////////////
////////////////////////////////////////////////////////////////////////

static cairo_surface_t *
cairo_surface_from_appsurf(struct app_surface *surf, struct wl_buffer *buffer)
{
	cairo_format_t format = translate_wl_shm_format(surf->pool->format);
	cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
		(unsigned char *)shm_pool_buffer_access(buffer),
		format, surf->w, surf->h,
		cairo_format_stride_for_width(format, surf->w));
	return cairo_surf;
}

int
shell_panel_paint_widget(struct shell_panel *panel, struct shell_widget *widget)
{
	struct app_surface *surf = &panel->panelsurf;
	const struct bbox *bbox = &widget->icon.box;

	if (surf->committed[1]) {
//		fprintf(stderr, "trying to paint committed surface\n");
		return 0;
	}
	cairo_surface_t *panelsurf = cairo_surface_from_appsurf(surf, surf->wl_buffer[1]);
	cairo_t *context = cairo_create(panelsurf);

	cairo_surface_t *iconsurf = widget->icon.isurf;
	//clean, paint and destroy
	cairo_rectangle(context, bbox->x, bbox->y, bbox->w, bbox->h);
	cairo_set_source_rgba(context, 1.0f, 1.0f, 1.0f, 1.0f);
	cairo_paint(context);
	cairo_set_source_surface(context, iconsurf, bbox->x, bbox->y);
	cairo_paint(context);
	surf->dirty[1] = true;
	//destroy cairo handles
	cairo_destroy(context);
	cairo_surface_destroy(panelsurf);
	return 1;
}

struct shell_widget *
shell_panel_find_widget_at_xy(struct shell_panel *panel, int x, int y)
{
	struct shell_widget *app;
	for (int i = 0; i < panel->widgets.len; i++) {
		app = (struct shell_widget *)vector_at(&panel->widgets, i);
		if (bbox_contain_point(&app->icon.box, x, y))
			return app;
	}
	return NULL;
}

int
shell_panel_n_widgets(struct shell_panel *panel)
{
	return panel->widgets.len;
}


struct shell_widget *
shell_panel_add_widget(struct shell_panel *surf)
{
	struct bbox box;
	struct shell_widget *last_widget, *new_widget;
	vector_t *widgets = &surf->widgets;
	if (!surf->widgets.elems)
		vector_init(&surf->widgets, sizeof(struct shell_widget), _free_widget);
	last_widget = (struct shell_widget *)vector_at(widgets, widgets->len-1);
	if (!last_widget) {
		box = (struct bbox) {
			.x = 0, .y = 0,
			.w = surf->panelsurf.w,
			.h = surf->panelsurf.h,
		};
	} else {
		const struct bbox *last_box  = &last_widget->icon.box;
		box = (struct bbox) {
			.x = 0, .y = 0,
			.w = last_box->x,
			.h = surf->panelsurf.h,
		};
	}
	new_widget = (struct shell_widget *)vector_newelem(widgets);
	*new_widget = (struct shell_widget){0};
	new_widget->icon.box = box;
	new_widget->panel = surf;
	new_widget->icon.box = box;

	return new_widget;
}

/* this is for backup */
/* void */
/* shell_panel_destroy_widgets(struct shell_panel *app) */
/* { */
/*	//this is indeed called */
/*	/\* fprintf(stderr, "shell_panel_destroy_widgets, this should get called\n"); *\/ */
/*	glDeleteBuffers(1, &app->vbo); */
/*	glDeleteBuffers(1, &app->ebo); */
/*	glDeleteVertexArrays(1, &app->vao); */
/*	glDeleteTextures(1, &app->font_tex); */
/*	glDeleteShader(app->vs); */
/*	glDeleteShader(app->fs); */
/*	glDeleteProgram(app->glprog); */
/*	vector_destroy(&app->widgets); */
/*	//destroy the egl context */
/*	nk_font_atlas_cleanup(&app->atlas); */
/*	nk_free(&app->ctx); */
/*	eglMakeCurrent(app->eglenv->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); */
/*	eglDestroySurface(app->eglenv->egl_display, app->eglsurface); */
/*	wl_egl_window_destroy(app->eglwin); */
/*	appsurface_destroy(&app->widget_surface); */
/* } */

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

	app->width = 50;
	app->width = 50;
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

	app->width = 50;
	app->width = 50;
	//register globals
	void *ptr = lua_newuserdata(app->L, sizeof(void *));
	lua_setglobal(app->L, "application");
	//TODO, we should call update icon for the first time
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


void
shell_panel_compile_widgets(struct shell_panel *panel)

{
	GLint status, loglen;
	struct app_surface *widget_surface = &panel->widget_surface;

	panel->eglwin = wl_egl_window_create(widget_surface->wl_surface, 50, 50);
	assert(panel->eglwin);
	panel->eglsurface = eglCreateWindowSurface(panel->eglenv->egl_display, panel->eglenv->config,
						   (EGLNativeWindowType)panel->eglwin, NULL);
	assert(panel->eglsurface);
	assert(eglMakeCurrent(panel->eglenv->egl_display, panel->eglsurface,
			      panel->eglsurface, panel->eglenv->egl_context));
	static const GLchar *vertex_shader =
		NK_SHADER_VERSION
		"uniform mat4 ProjMtx;\n"
		"in vec2 Position;\n"
		"in vec2 TexCoord;\n"
		"in vec4 Color;\n"
		"out vec2 Frag_UV;\n"
		"out vec4 Frag_Color;\n"
		"void main() {\n"
		"   Frag_UV = TexCoord;\n"
		"   Frag_Color = Color;\n"
		"   gl_Position = ProjMtx * vec4(Position.xy, 0, 1);\n"
		"}\n";
	static const GLchar *fragment_shader =
		NK_SHADER_VERSION
		"precision mediump float;\n"
		"uniform sampler2D Texture;\n"
		"in vec2 Frag_UV;\n"
		"in vec4 Frag_Color;\n"
		"out vec4 Out_Color;\n"
		"void main(){\n"
		"   Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
		"}\n";
	panel->glprog = glCreateProgram();
	panel->vs = glCreateShader(GL_VERTEX_SHADER);
	panel->fs = glCreateShader(GL_FRAGMENT_SHADER);
	assert(glGetError() == GL_NO_ERROR);
	glShaderSource(panel->vs, 1, &vertex_shader, 0);
	glShaderSource(panel->fs, 1, &fragment_shader, 0);
	glCompileShader(panel->vs);
	glCompileShader(panel->fs);
	glGetShaderiv(panel->vs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(panel->vs, GL_INFO_LOG_LENGTH, &loglen);
	/* if (status != GL_TRUE) { */
	/*	char err_msg[loglen]; */
	/*	glGetShaderInfoLog(panel->vs, loglen, NULL, err_msg); */
	/*	fprintf(stderr, "vertex shader compile fails: %s\n", err_msg); */
	/* } */
	assert(status == GL_TRUE);
	glGetShaderiv(panel->fs, GL_COMPILE_STATUS, &status);
	glGetShaderiv(panel->fs, GL_INFO_LOG_LENGTH, &loglen);
	assert(status == GL_TRUE);
	//link shader into program
	glAttachShader(panel->glprog, panel->vs);
	glAttachShader(panel->glprog, panel->fs);
	glLinkProgram(panel->glprog);
	glGetProgramiv(panel->glprog, GL_LINK_STATUS, &status);
	assert(status == GL_TRUE);
	//get all the uniforms
	glUseProgram(panel->glprog);
	panel->uniform_tex = glGetUniformLocation(panel->glprog, "Texture");
	panel->uniform_proj = glGetUniformLocation(panel->glprog, "ProjMtx");
	panel->attrib_pos = glGetAttribLocation(panel->glprog, "Position");
	panel->attrib_uv = glGetAttribLocation(panel->glprog, "TexCoord");
	panel->attrib_col = glGetAttribLocation(panel->glprog, "Color");
	//asserts
	assert(panel->uniform_tex >= 0);
	assert(panel->uniform_proj >= 0);
	assert(panel->attrib_pos >= 0);
	assert(panel->attrib_pos >= 0);
	assert(panel->attrib_uv  >= 0);
	//vertex array
	GLsizei stride = sizeof(struct egl_nk_vertex);
	off_t vp = offsetof(struct egl_nk_vertex, position);
	off_t vt = offsetof(struct egl_nk_vertex, uv);
	off_t vc = offsetof(struct egl_nk_vertex, col);

	glGenVertexArrays(1, &panel->vao);
	glGenBuffers(1, &panel->vbo);
	glGenBuffers(1, &panel->ebo);
	glBindVertexArray(panel->vao);
	glBindBuffer(GL_ARRAY_BUFFER, panel->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, panel->ebo);

	glVertexAttribPointer(panel->attrib_pos, 2, GL_FLOAT, GL_FALSE,
			      stride, (void *)vp);
	glVertexAttribPointer(panel->attrib_uv, 2, GL_FLOAT, GL_FALSE,
			      stride, (void *)vt);
	glVertexAttribPointer(panel->attrib_col, 4, GL_FLOAT, GL_FALSE,
			      stride, (void *)vc);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	//at draw call, you will need to came up with all the uniforms
	glUseProgram(0);
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

void
shell_panel_destroy_widgets(struct shell_panel *app)
{
	glDeleteBuffers(1, &app->vbo);
	glDeleteBuffers(1, &app->ebo);
	glDeleteVertexArrays(1, &app->vao);
	glDeleteTextures(1, &app->font_tex);
	glDeleteShader(app->vs);
	glDeleteShader(app->fs);
	glDeleteProgram(app->glprog);
	vector_destroy(&app->widgets);
}

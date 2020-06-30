//we include thisr first so we can pull in the opengl prototypes

#include <twclient/glhelper.h>
#include <twclient/egl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <twclient/client.h>
#include <twclient/shmpool.h>
#include <wayland-xdg-shell-client-protocol.h>

static struct wl_shell *s_wl_shell = NULL;
static struct xdg_wm_base *s_xdg_wm_base = NULL;
static bool s_configured = false;


static struct quad_shader {
	GLuint prog;
	GLuint vao, vbo;
} s_quad_shader = {0};

static void
dummy_draw(struct tw_appsurf *surf, struct tw_bbox *geo)
{
	glViewport(0, 0, geo->w, geo->h);

	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	glUseProgram(s_quad_shader.prog);
	/* glDisable(GL_BLEND); */
	/* glDisable(GL_DEPTH_TEST); */
	glBindVertexArray(s_quad_shader.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s_quad_shader.vbo);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glUseProgram(0);
	wl_surface_set_buffer_transform(surf->wl_surface,
	                                WL_OUTPUT_TRANSFORM_90);

}

#define GL_SHADER_VERSION "#version 330 core\n"
#define GLES_SHADER_VERSION "#version 100\n"

#if defined (_TW_USE_GL)

static const GLchar *glsl_vs =
	GL_SHADER_VERSION
	"in vec2 Position;\n"
	"in vec4 Color;\n"
	"out vec4 Frag_Color;\n"
	"void main() {\n"
	"   Frag_Color = Color;\n"
	"   gl_Position = vec4(Position.xy, 0, 1);\n"
	"}\n";

static const GLchar *glsl_fs =
	GL_SHADER_VERSION
	"precision mediump float;\n"
	"uniform sampler2D Texture;\n"
	"in vec4 Frag_Color;\n"
	"out vec4 Out_Color;\n"
	"void main(){\n"
	"   Out_Color = Frag_Color;\n"
	"}\n";

#elif defined(_TW_USE_GLES)

static const GLchar *gles_vs =
	GLES_SHADER_VERSION
	"uniform mat4 ProjMtx;\n"
	"attribute vec2 Position;\n"
	"attribute vec4 Color;\n"
	"varying vec4 Frag_Color;\n"
	"void main() {\n"
	"   Frag_Color = Color;\n"
	"   gl_Position = vec4(Position.xy, 0, 1);\n"
	"}\n";

static const GLchar *gles_fs =
	GLES_SHADER_VERSION
	"precision mediump float;\n"
	"uniform sampler2D Texture;\n"
	"varying vec4 Frag_Color;\n"
	"void main(){\n"
	"   gl_FragColor = Frag_Color;\n"
	"}\n";
#endif

static void
dummy_prepare()
{
	GLfloat verts[] = {
		1.0, 1.0, 1.0, 0.0, 0.0, 1.0, //top-right
		-1.0, 1.0, 0.0, 1.0, 0.0, 1.0, //top-left
		1.0, -1.0, 0.0, 0.0, 1.0, 1.0, //bottom-right,
		-1.0, -1.0, 1.0, 1.0, 0.0, 1.0, //bottom-left
	};

#if defined (_TW_USE_GL)
	s_quad_shader.prog = tw_gl_create_program(glsl_vs, glsl_fs,
	                                          NULL, NULL, NULL);
#elif defined(_TW_USE_GLES)
	s_quad_shader.prog = tw_gl_create_program(gles_vs, gles_fs,
	                                          NULL, NULL, NULL);
#endif
	assert(s_quad_shader.prog);

	glGenVertexArrays(1, &s_quad_shader.vao);
	glGenBuffers(1, &s_quad_shader.vbo);
	assert(s_quad_shader.vao);
	assert(s_quad_shader.vbo);

	glBindVertexArray(s_quad_shader.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s_quad_shader.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
	                      6 * sizeof(float), (GLvoid *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
	                      6 * sizeof(float), (void *)(2 * sizeof(float)));
	glBindVertexArray(0);
}

static void
dummy_release()
{
	glDeleteProgram(s_quad_shader.prog);
	glDeleteVertexArrays(1, &s_quad_shader.vao);
	glDeleteBuffers(1, &s_quad_shader.vbo);
}

/***************************  wl_shell impls  *****************************/
static void
handle_shell_ping(void *data, struct wl_shell_surface *wl_shell_surface,
                  uint32_t serial)
{
	wl_shell_surface_pong(wl_shell_surface, serial);
}

static void
handle_shell_configure(void *data, struct wl_shell_surface *shell_surface,
                       uint32_t edges, int32_t width, int32_t height)
{
	struct tw_appsurf *app = data;
	tw_appsurf_resize(app, width, height, app->allocation.s);
}

static void
shell_handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}


static struct wl_shell_surface_listener shell_impl = {
	.ping = handle_shell_ping,
	.configure = handle_shell_configure,
	.popup_done = shell_handle_popup_done,
};

/***************************  wl_shell impls  *****************************/


static void
handle_toplevel_configure(void *data,
			 struct xdg_toplevel *xdg_toplevel,
			 int32_t width,
			 int32_t height,
			 struct wl_array *states)
{
	struct tw_appsurf *app = data;

	if (!width || !height)
		return;
	tw_appsurf_resize(app, width, height, app->allocation.s);
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct xdg_surface *xdg_surface = data;
	struct tw_appsurf *app =
		xdg_surface_get_user_data(xdg_surface);
	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	tw_appsurf_release(app);
}

static const struct xdg_toplevel_listener toplevel_impl = {
	.configure = handle_toplevel_configure,
	.close = handle_toplevel_close,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                      uint32_t serial)
{
	struct tw_appsurf *app = data;
	app->tw_globals->inputs.serial = serial;
	xdg_surface_ack_configure(xdg_surface, serial);
	s_configured = true;
}

static struct xdg_surface_listener xdg_impl = {
	.configure = xdg_surface_configure,
};

static void
xdg_wm_ping(void *data,
            struct xdg_wm_base *xdg_wm_base,
            uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}


static struct xdg_wm_base_listener xdg_wm_impl = {
	.ping = xdg_wm_ping,
};

/*************************** initializations  *****************************/

static
void announce_globals(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	struct tw_globals *globals = data;
	if (strcmp(interface, wl_shell_interface.name) == 0) {
		s_wl_shell = wl_registry_bind(wl_registry, name,
		                             &wl_shell_interface, version);
		fprintf(stdout, "wl_shell %d announced\n", name);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		s_xdg_wm_base = wl_registry_bind(wl_registry, name,
		                                 &xdg_wm_base_interface,
		                                 version);
		xdg_wm_base_add_listener(s_xdg_wm_base, &xdg_wm_impl, globals);
	}

	tw_globals_announce(globals, wl_registry, name, interface, version);
}

static
void announce_global_remove(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name)
{
	fprintf(stderr, "global %d removed", name);
}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};


int main(int argc, char *argv[])
{
	struct tw_appsurf app;
	struct tw_globals tw_globals;
	struct wl_shell_surface *shell_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;

	struct wl_display *wl_display = wl_display_connect(NULL);
	tw_globals_init(&tw_globals, wl_display);
	if (!wl_display) {
		fprintf(stderr, "no display available\n");
		return -1;
	}
	struct tw_egl_env env;
	if (!tw_egl_env_init(&env, wl_display))
		return -1;
	eglMakeCurrent(env.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	               env.egl_context);

	struct wl_registry *registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(registry, &registry_listener, &tw_globals);

	wl_display_dispatch(wl_display);
	wl_display_roundtrip(wl_display);

	struct wl_surface *surface =
		wl_compositor_create_surface(tw_globals.compositor);
	tw_appsurf_init(&app, surface, &tw_globals, TW_APPSURF_WIDGET, 0);
	eglwin_impl_app_surface(&app, dummy_draw,
	                        tw_make_bbox_origin(400, 200, 1), &env);
	wl_display_flush(wl_display);

	dummy_prepare();

	if (s_wl_shell) {
		shell_surface = wl_shell_get_shell_surface(s_wl_shell, surface);
		wl_shell_surface_set_toplevel(shell_surface);
		wl_shell_surface_add_listener(shell_surface, &shell_impl, &app);
	} else if (s_xdg_wm_base) {
		xdg_surface = xdg_wm_base_get_xdg_surface(s_xdg_wm_base,
		                                          surface);
		toplevel = xdg_surface_get_toplevel(xdg_surface);
		xdg_surface_add_listener(xdg_surface, &xdg_impl, &app);
		xdg_toplevel_add_listener(toplevel, &toplevel_impl, &app);

		wl_surface_commit(surface);
		wl_display_flush(wl_display);

		while (!s_configured)
			wl_display_dispatch(wl_display);
	}

	tw_appsurf_frame(&app, false);

	tw_globals_dispatch_event_queue(&tw_globals);

	dummy_release();
	tw_globals_release(&tw_globals);
	tw_egl_env_end(&env);
	wl_registry_destroy(registry);
	wl_display_disconnect(wl_display);

	return 0;
}

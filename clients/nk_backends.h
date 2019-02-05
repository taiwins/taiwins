#ifndef NK_BACKENDS_H
#define NK_BACKENDS_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "ui.h"

struct nk_wl_backend;
struct nk_context;
struct nk_style;

#ifdef NK_PRIAVTE
/* if we use nk_private, then we define everything into static */

#if defined(NK_INCLUDE_CAIRO_BACKEND)
#include "nuklear/nk_wl_cairo.c"

#elif defined(NK_INCLUDE_EGL_BACKEND)
#include "nuklear/nk_wl_egl.c"

#elif defined (NK_INCLUDE_VK_BACKEND)
#include "nuklear/nk_wl_vulkan.c"

#endif /* include backends */

#else /* NK_PRIVATE */

#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS

#include "../3rdparties/nuklear/nuklear.h"

typedef void (*nk_wl_drawcall_t)(struct nk_context *ctx, float width, float height, struct app_surface *app);
typedef void (*nk_wl_postcall_t)(struct app_surface *app);

/* cairo_backend */
struct nk_wl_backend *nk_cairo_create_bkend(void);
void nk_cairo_destroy_bkend(struct nk_wl_backend *bkend);

void
nk_cairo_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			  nk_wl_drawcall_t draw_cb, struct shm_pool *pool,
			  uint32_t w, uint32_t h, uint32_t x, uint32_t y,
			  int32_t s);


/* egl_backend */
struct nk_wl_backend* nk_egl_create_backend(const struct wl_display *display,
					    const struct egl_env *shared_env);

void nk_egl_destroy_backend(struct nk_wl_backend *b);
void
nk_egl_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			nk_wl_drawcall_t draw_cb,
			uint32_t w, uint32_t h, uint32_t x, uint32_t y,
			int32_t s);

/* vulkan_backend */
struct nk_wl_backend *nk_vulkan_backend_create(void);
struct nk_wl_backend *nk_vulkan_backend_clone(struct nk_wl_backend *b);
void nk_vulkan_backend_destroy(struct nk_wl_backend *b);
void
nk_vulkan_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			   nk_wl_drawcall_t draw_cb,
			   uint32_t w, uint32_t h, uint32_t x, uint32_t y);


#endif /* NK_PRIVATE */

#ifndef NK_API
#define NK_API
#endif

NK_API xkb_keysym_t nk_wl_get_keyinput(struct nk_context *ctx);

NK_API bool
nk_wl_get_btn(struct nk_context *ctx, uint32_t *button, uint32_t *sx, uint32_t *sy);

NK_API void
nk_wl_add_idle(struct nk_context *ctx, nk_wl_postcall_t task);


NK_API const struct nk_style *
nk_wl_get_curr_style(struct nk_wl_backend *bkend);

NK_API void
nk_wl_test_draw(struct nk_wl_backend *bkend, struct app_surface *app,
		nk_wl_drawcall_t draw_call);

#endif

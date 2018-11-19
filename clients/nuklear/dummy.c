#include "../client.h"
#include "../ui.h"

struct nk_wl_bkend;
struct nk_context;
struct nk_wl_backend * nk_cairo_create_bkend(void);

typedef void (*nk_wl_drawcall_t)(struct nk_context *ctx, float width, float height, struct app_surface *app);


void
nk_cairo_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			  nk_wl_drawcall_t draw_cb, struct shm_pool *pool,
			  uint32_t w, uint32_t h, uint32_t x, uint32_t y);



int main(int argc, char *argv[])
{
	struct app_surface app_surface;
	struct nk_wl_backend *dummy = nk_cairo_create_bkend();
	nk_cairo_impl_app_surface(&app_surface, dummy, NULL, NULL, 100, 100, 0, 0);
	return 0;
}

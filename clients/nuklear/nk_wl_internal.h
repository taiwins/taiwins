#ifndef NK_WL_INTERNAL_H
#define NK_WL_INTERNAL_H

/* this file should have contained a an template struct which supposed to be
 * accessible all nuklear_backends here.
 *
 * declare NK_IMPLEMENTATION and other specific maro before INCLUDE this file !
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-client.h>


/* we include this so we do not need to deal with namespace collision, in the
 * mean time, for the sake private implementation, all the function declared
 * here should be declared as static.
 */
//#define NK_PRIVATE
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS

//this will make our struct various size, so lets put the buffer in the end
#define NK_MAX_CTX_MEM 64 * 64 * 1024


#include "../../3rdparties/nuklear/nuklear.h"
#include "../client.h"
#include "../ui.h"

//we may in the end include that file so save all the trouble
typedef void (*nk_wl_drawcall_t)(struct nk_context *ctx, float width, float height, struct app_surface *app);
typedef void (*nk_wl_postcall_t)(struct app_surface *app);

#ifndef NK_MAX_CMD_SIZE
#define NK_MAX_CMD_SIZE (sizeof (union {	\
	struct nk_command_scissor s;		\
	struct nk_command_line l;		\
	struct nk_command_curve q;		\
	struct nk_command_rect r;		\
	struct nk_command_rect_filled rf;	\
	struct nk_command_rect_multi_color rmc; \
	struct nk_command_triangle t;		\
	struct nk_command_triangle_filled tf;	\
	struct nk_command_circle c;		\
	struct nk_command_circle_filled cf;	\
	struct nk_command_arc a;		\
	struct nk_command_arc_filled af;	\
	struct nk_command_polygon pg;		\
	struct nk_command_polygon_filled pgf;	\
	struct nk_command_polyline pl;		\
	struct nk_command_image im;		\
	struct nk_command_text txt;		\
	struct nk_command_custom cus;		\
	}) )
typedef unsigned char nk_max_cmd_t[NK_MAX_CMD_SIZE];
#endif /* NK_MAX_CMD_SIZE */

struct nk_wl_backend {
	//I will need to make this ctx a pointer to have nk_wl_backend a
	//consistant size
	struct nk_context ctx;
	struct nk_buffer cmds;

	struct nk_colorf main_color;
	nk_rune *unicode_range;


	//update to date information
	struct app_surface *app_surface;
	nk_wl_drawcall_t frame;
	nk_wl_postcall_t post_cb;


	struct {
		xkb_keysym_t ckey; //cleaned up every frame
		int32_t cbtn; //clean up every frame
		uint32_t sx;
		uint32_t sy;
	};
	//data
	unsigned char ctx_buffer[NK_MAX_CTX_MEM];
};

/******************************** render *******************************************/
static void nk_wl_render(struct nk_wl_backend *bkend);
static void nk_wl_call_preframe(struct nk_wl_backend *bkend,
				struct app_surface *surf);

static void
nk_wl_new_frame(struct app_surface *surf, uint32_t user_data)
{
	//here is how we manage the buffer
	struct nk_wl_backend *bkend = surf->user_data;
	int width = bkend->app_surface->w;
	int height = bkend->app_surface->h;

	if (surf->need_animation)
		app_surface_request_frame(surf);
	//so here we still need a hook
	nk_wl_call_preframe(bkend, surf);

	if (nk_begin(&bkend->ctx, "cairo_app", nk_rect(0, 0, width, height),
		     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
		bkend->frame(&bkend->ctx, width, height, bkend->app_surface);
	} nk_end(&bkend->ctx);

	nk_wl_render(bkend);
	//text edit has problems, I don't think it is here though
	nk_clear(&bkend->ctx);

	if (bkend->post_cb) {
		bkend->post_cb(bkend->app_surface);
		bkend->post_cb = NULL;
	}
	bkend->ckey = XKB_KEY_NoSymbol;
	bkend->cbtn = -1;
}

static inline bool
nk_wl_need_redraw(struct nk_wl_backend *bkend)
{
	static nk_max_cmd_t nk_last_draw[100] = {0};
//	static char nk_last_draw[NK_MAX_CMD_SIZE * 100] = {0};
	void *cmds = nk_buffer_memory(&bkend->ctx.memory);
	bool need_redraw = memcmp(cmds, nk_last_draw, bkend->ctx.memory.allocated);
	if (need_redraw)
		memcpy(nk_last_draw, cmds, bkend->ctx.memory.allocated);
	return need_redraw;
}

/******************************** nuklear colors ***********************************/
static inline void
nk_color_from_tw_rgba(struct nk_color *nc, const struct tw_rgba_t *tc)
{
	nc->r = tc->r; nc->g = tc->g;
	nc->b = tc->b; nc->a = tc->a;
}

static inline void
nk_colorf_from_tw_rgba(struct nk_colorf *nc, const struct tw_rgba_t *tc)
{
	nc->r = (float)tc->r/255.0; nc->g = (float)tc->g/255.0;
	nc->b = (float)tc->b/255.0; nc->a = (float)tc->a/255.0;
}


static void
nk_wl_apply_color(struct nk_wl_backend *bkend, const struct taiwins_theme *theme)
{
	if (theme->row_size == 0)
		return;

	//TODO this is a shitty hack, somehow the first draw call did not work, we
	//have to hack it in the background color
	nk_colorf_from_tw_rgba(&bkend->main_color, &theme->window_color);
	struct nk_color table[NK_COLOR_COUNT];
	nk_color_from_tw_rgba(&table[NK_COLOR_TEXT], &theme->text_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_WINDOW], &theme->window_color);
	//no header
	nk_color_from_tw_rgba(&table[NK_COLOR_HEADER], &theme->window_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_BORDER], &theme->border_color);
	//button
	nk_color_from_tw_rgba(&table[NK_COLOR_BUTTON], &theme->button.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_BUTTON_HOVER],
			      &theme->button.hover);
	nk_color_from_tw_rgba(&table[NK_COLOR_BUTTON_ACTIVE],
			      &theme->button.active);
	//toggle
	nk_color_from_tw_rgba(&table[NK_COLOR_TOGGLE],
			      &theme->toggle.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_TOGGLE_HOVER],
			      &theme->toggle.hover);
	nk_color_from_tw_rgba(&table[NK_COLOR_TOGGLE_CURSOR],
			      &theme->toggle.active);
	//select
	nk_color_from_tw_rgba(&table[NK_COLOR_SELECT],
			      &theme->select.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_SELECT_ACTIVE],
			      &theme->select.active);
	//slider
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER],
			      &theme->slider_bg_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER_CURSOR],
			      &theme->slider.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER_CURSOR_HOVER],
			      &theme->slider.hover);
	nk_color_from_tw_rgba(&table[NK_COLOR_SLIDER_CURSOR_ACTIVE],
			      &theme->slider.active);
	//property
	table[NK_COLOR_PROPERTY] = table[NK_COLOR_SLIDER];
	//edit
	nk_color_from_tw_rgba(&table[NK_COLOR_EDIT], &theme->text_active_color);
	nk_color_from_tw_rgba(&table[NK_COLOR_EDIT_CURSOR], &theme->text_color);
	//combo
	nk_color_from_tw_rgba(&table[NK_COLOR_COMBO], &theme->combo_color);
	//chart
	nk_color_from_tw_rgba(&table[NK_COLOR_CHART], &theme->chart.normal);
	nk_color_from_tw_rgba(&table[NK_COLOR_CHART_COLOR], &theme->chart.active);
	nk_color_from_tw_rgba(&table[NK_COLOR_CHART_COLOR_HIGHLIGHT],
			      &theme->chart.hover);
	//scrollbar
	table[NK_COLOR_SCROLLBAR] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_SCROLLBAR_CURSOR] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = table[NK_COLOR_WINDOW];
	table[NK_COLOR_TAB_HEADER] = table[NK_COLOR_WINDOW];
	nk_style_from_table(&bkend->ctx, table);
}


/********************************* unicode ****************************************/
static inline void
union_unicode_range(const nk_rune left[2], const nk_rune right[2], nk_rune out[2])
{
	nk_rune tmp[2];
	tmp[0] = left[0] < right[0] ? left[0] : right[0];
	tmp[1] = left[1] > right[1] ? left[1] : right[1];
	out[0] = tmp[0];
	out[1] = tmp[1];
}

//return true if left and right are insersected, else false
static inline bool
intersect_unicode_range(const nk_rune left[2], const nk_rune right[2])
{
	return (left[0] <= right[1] && left[1] >= right[1]) ||
		(left[0] <= right[0] && left[1] >= right[0]);
}

static inline int
unicode_range_compare(const void *l, const void *r)
{
	const nk_rune *range_left = (const nk_rune *)l;
	const nk_rune *range_right = (const nk_rune *)r;
	return ((int)range_left[0] - (int)range_right[0]);
}

static int
merge_unicode_range(const nk_rune *left, const nk_rune *right, nk_rune *out)
{
	//get the range
	int left_size = 0;
	while(*(left+left_size)) left_size++;
	int right_size = 0;
	while(*(right+right_size)) right_size++;
	//sort the range,
	nk_rune sorted_ranges[left_size+right_size];
	memcpy(sorted_ranges, left, sizeof(nk_rune) * left_size);
	memcpy(sorted_ranges+left_size, right, sizeof(nk_rune) * right_size);
	qsort(sorted_ranges, (left_size+right_size)/2, sizeof(nk_rune) * 2,
	      unicode_range_compare);
	//merge algorithm
	nk_rune merged[left_size+right_size+1];
	merged[0] = sorted_ranges[0];
	merged[1] = sorted_ranges[1];
	int m = 0;
	for (int i = 0; i < (left_size+right_size) / 2; i++) {
		if (intersect_unicode_range(&sorted_ranges[i*2],
					    &merged[2*m]))
			union_unicode_range(&sorted_ranges[i*2], &merged[2*m],
					    &merged[2*m]);
		else {
			m++;
			merged[2*m] = sorted_ranges[2*i];
			merged[2*m+1] = sorted_ranges[2*i+1];
		}
	}
	m++;
	merged[2*m] = 0;

	if (!out)
		return 2*m;
	memcpy(out, merged, (2*m+1) * sizeof(nk_rune));
	return 2*m;
}

/********************************* input ****************************************/

static void
nk_keycb(struct app_surface *surf, xkb_keysym_t keysym, uint32_t modifier, int state)
{
	//nk_input_key and nk_input_unicode are different, you kinda need to
	//registered all the keys
	struct nk_wl_backend *bkend = (struct nk_wl_backend *)surf->user_data;
	uint32_t keycode = xkb_keysym_to_utf32(keysym);
	nk_input_begin(&bkend->ctx);
	//now we deal with the ctrl-keys
	if (modifier & TW_CTRL) {
		//the emacs keybindings
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_START, (keysym == XKB_KEY_a) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_END, (keysym == XKB_KEY_e) && state);
		nk_input_key(&bkend->ctx, NK_KEY_LEFT, (keysym == XKB_KEY_b) && state);
		nk_input_key(&bkend->ctx, NK_KEY_RIGHT, (keysym == XKB_KEY_f) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_UNDO, (keysym == XKB_KEY_slash) && state);
		//we should also support the clipboard later
	}
	else if (modifier & TW_ALT) {
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_WORD_LEFT, (keysym == XKB_KEY_b) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_WORD_RIGHT, (keysym == XKB_KEY_f) && state);
	}
	//no tabs, we don't essentially need a buffer here, give your own buffer. That is it.
	else if (keycode >= 0x20 && keycode < 0x7E && state)
		nk_input_unicode(&bkend->ctx, keycode);
	else {
		nk_input_key(&bkend->ctx, NK_KEY_DEL, (keysym == XKB_KEY_Delete) && state);
		nk_input_key(&bkend->ctx, NK_KEY_ENTER, (keysym == XKB_KEY_Return) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TAB, keysym == XKB_KEY_Tab && state);
		nk_input_key(&bkend->ctx, NK_KEY_BACKSPACE, (keysym == XKB_KEY_BackSpace) && state);
		nk_input_key(&bkend->ctx, NK_KEY_UP, (keysym == XKB_KEY_UP) && state);
		nk_input_key(&bkend->ctx, NK_KEY_DOWN, (keysym == XKB_KEY_DOWN) && state);
		nk_input_key(&bkend->ctx, NK_KEY_SHIFT, (keysym == XKB_KEY_Shift_L ||
							 keysym == XKB_KEY_Shift_R) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_START, (keysym == XKB_KEY_Home) && state);
		nk_input_key(&bkend->ctx, NK_KEY_TEXT_LINE_END, (keysym == XKB_KEY_End) && state);
		nk_input_key(&bkend->ctx, NK_KEY_LEFT, (keysym == XKB_KEY_Left) && state);
		nk_input_key(&bkend->ctx, NK_KEY_RIGHT, (keysym == XKB_KEY_Right) && state);
	}
//	fprintf(stderr, "we have the modifier %d\n", modifier);
	if (state)
		bkend->ckey = keysym;
	else
		bkend->ckey = XKB_KEY_NoSymbol;
	nk_input_end(&bkend->ctx);

	nk_wl_new_frame(surf, 0);
}

static void
nk_pointron(struct app_surface *surf, uint32_t sx, uint32_t sy)
{
	struct nk_wl_backend *bkend = (struct nk_wl_backend *)surf->user_data;
	nk_input_begin(&bkend->ctx);
	nk_input_motion(&bkend->ctx, sx, sy);
	nk_input_end(&bkend->ctx);
	bkend->sx = sx;
	bkend->sy = sy;
	nk_wl_new_frame(surf, 0);
}

static void
nk_pointrbtn(struct app_surface *surf, enum taiwins_btn_t btn, bool state, uint32_t sx, uint32_t sy)
{
	struct nk_wl_backend *bkend = (struct nk_wl_backend *)surf->user_data;
	enum nk_buttons b;
	switch (btn) {
	case TWBTN_LEFT:
		b = NK_BUTTON_LEFT;
		break;
	case TWBTN_RIGHT:
		b = NK_BUTTON_RIGHT;
		break;
	case TWBTN_MID:
		b = NK_BUTTON_MIDDLE;
		break;
	case TWBTN_DCLICK:
		b = NK_BUTTON_DOUBLE;
		break;
	}

	nk_input_begin(&bkend->ctx);
	nk_input_button(&bkend->ctx, b, (int)sx, (int)sy, state);
	nk_input_end(&bkend->ctx);

	bkend->cbtn = (state) ? b : -1;
	bkend->sx = sx;
	bkend->sy = sy;

	nk_wl_new_frame(surf, 0);
}

static void
nk_pointraxis(struct app_surface *surf, int pos, int direction, uint32_t sx, uint32_t sy)
{
	struct nk_wl_backend *bkend = (struct nk_wl_backend *)surf->user_data;
	nk_input_begin(&bkend->ctx);
	nk_input_scroll(&bkend->ctx, nk_vec2(direction * (float)sx, (direction * (float)sy)));
	nk_input_begin(&bkend->ctx);
	nk_wl_new_frame(surf, 0);
}


/********************************* setup *******************************************/
static void
nk_wl_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
		       nk_wl_drawcall_t draw_cb, uint32_t w, uint32_t h,
		       uint32_t x, uint32_t y)
{
	surf->w = w;
	surf->h = h;
	surf->px = x;
	surf->py = y;
	surf->s = 1;
	surf->user_data = bkend;
	surf->do_frame = nk_wl_new_frame;
	//input
	surf->keycb = nk_keycb;
	surf->pointrbtn = nk_pointrbtn;
	surf->pointron = nk_pointron;
	surf->pointraxis = nk_pointraxis;
	//change the current state of the backend
	bkend->frame = draw_cb;
	bkend->cbtn = -1;
	bkend->ckey = XKB_KEY_NoSymbol;
	bkend->app_surface = surf;

	if (surf->wl_globals) {
		nk_wl_apply_color(bkend, &surf->wl_globals->theme);
		//TODO try to apply the font here as well.
	}
}

static void
nk_wl_clean_app_surface(struct nk_wl_backend *bkend)
{
	bkend->frame = NULL;
	bkend->cbtn = -1;
	bkend->ckey = XKB_KEY_NoSymbol;
	bkend->app_surface->do_frame = NULL;
	bkend->app_surface->user_data = NULL;
	bkend->app_surface = NULL;

}



//there are quite a few code we can write here for sure.

#endif /* EOF */

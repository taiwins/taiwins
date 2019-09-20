#include "../../3rdparties/iconheader/IconsFontAwesome5_c.h"
#include "../widget.h"

/*
 * This is a simple illustration of how to work with a row. We may really does
 * not know how much space the widget occupies inadvance.
 *
 * the widget should start with nk_layout_row_push to occupy its space, we limit
 * one widget right now, as it is what most time is. But we may need to have
 * more versatile approach
 */
static int
clock_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	//it seems to work, we still need a library to figure out the text size
	/* nk_layout_row_push(ctx, 100); */
	static const char * daysoftheweek[] =
		{"sun", "mon", "tus", "wed", "thu", "fri", "sat"};
	time_t epochs = time(NULL);
	struct tm *tim = localtime(&epochs);

	return snprintf(label->label, 255, "%s %02d:%02d:%02d",
		       daysoftheweek[tim->tm_wday], tim->tm_hour, tim->tm_min, tim->tm_sec);
}

static void
clock_widget_sample(struct nk_context *ctx, float width, float height, struct app_surface *app)
{
	/* enum nk_buttons btn; */
	/* uint32_t sx, sy; */
	//TODO, change the draw function to app->draw_widget(app);
	enum {EASY, HARD};
	static struct nk_text_edit text_edit;
	static bool inanimation = false;
	static bool init_text_edit = false;
	static char text_buffer[256];
	static int checked = false;
	static bool active;
	if (!init_text_edit) {
		init_text_edit = true;
		nk_textedit_init_fixed(&text_edit, text_buffer, 256);
	}
	bool last_frame = inanimation;

	float spans[] = {0.5, 0.5};
	nk_layout_row(ctx, NK_DYNAMIC, 30, 2, spans);
	/* nk_layout_row_static(ctx, 30, 80, 2); */
	inanimation = //nk_button_symbol(ctx, NK_SYMBOL_X) ? !inanimation : inanimation;
		nk_button_symbol_label(ctx, NK_SYMBOL_TRIANGLE_UP, "a", NK_TEXT_ALIGN_MIDDLE) ? !inanimation : inanimation;
	if (inanimation && !last_frame)
		app_surface_request_frame(app);
	else if (!inanimation)
		app_surface_end_frame_request(app);
	active = nk_option_label(ctx, "another", active);

	checked = nk_radio_label(ctx, "radio", &checked);

	/* nk_layout_row_dynamic(ctx, 30, 2); */
	/* if (nk_option_label(ctx, "easy", op == EASY)) op = EASY; */
	/* if (nk_option_label(ctx, "hard", op == HARD)) op = HARD; */

	nk_layout_row_dynamic(ctx, 30, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &text_edit, nk_filter_default);
	/* nk_layout_row_dynamic(ctx, 25, 1); */
	/* selected = nk_select_symbol_label(ctx, NK_SYMBOL_TRIANGLE_LEFT, "select me", NK_TEXT_RIGHT, selected); */

	/* nk_color_pick(ctx, &color, NK_RGB); */
	/* nk_slider_int(ctx, 0, &slider, 16, 1); */

}

struct shell_widget clock_widget = {
	.ancre_cb = clock_widget_anchor,
	.draw_cb = clock_widget_sample,
	.w = 200,
	.h = 150,
	.interval = {
		.it_value = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
		.it_interval = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
	},
	.file_path = NULL,
};

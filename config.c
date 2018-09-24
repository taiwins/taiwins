#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include "config.h"

static bool
validate_theme_font(struct taiwins_theme *theme)
{
	FcInit();
	FcConfig *config = FcConfigGetCurrent();
	char *font_names[3] = {
		theme->ascii_font,
		theme->cjk_font,
		theme->icons_font,
	};
	for (int i = 0; i < 3; i++) {
		if (strlen(font_names[i]) == 0)
			continue;
		FcChar8 *file = NULL;
		const char *path;
		FcResult result;
		FcPattern *pat = FcNameParse((const FcChar8 *)font_names[i]);
		FcConfigSubstitute(config, pat, FcMatchPattern);
		FcDefaultSubstitute(pat);

		FcPattern *font = FcFontMatch(config, pat, &result);
		if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch) {
			path = (const char *)file;
			assert(strlen(path) < 255);
			strcpy(font_names[i], path);
		}
		FcPatternDestroy(font);
		FcPatternDestroy(pat);
	}
	FcFini();
	if (strlen(font_names[0]) == 0 ||
	    strlen(font_names[2]) == 0)
		return false;

	return true;
}

static inline float rgb2grayscale(struct tw_rgba_t *rgba)
{
	return (0.2126 * ((float)rgba->r / 255.0) +
		0.7152 * ((float)rgba->g / 255.0) +
		0.0722 * ((float)rgba->b / 255.0)) * ((float)rgba->a / 255.0);
}

static bool
validate_theme_colors(struct taiwins_theme *theme)
{
	//make sure they are distinguishable
	/* float ctext_orig = rgb2grayscale(&theme->text_color); */
	/* float ctext_active = rgb2grayscale(&theme->text_active_color); */
	/* float ctext_hover = rgb2grayscale(&theme->text_hover_color); */

	/* float cgui_orig = rgb2grayscale(&theme->gui_color); */
	/* float cgui_active = rgb2grayscale(&theme->gui_active_color); */
	/* float cgui_hover = rgb2grayscale(&theme->gui_hover_color); */

	/* struct itensity_pair { */
	/*	float l, r; */
	/* }; */
	/* struct itensity_pair text_guis[3] = { */
	/*	{ctext_orig, cgui_orig}, */
	/*	{ctext_active, cgui_active}, */
	/*	{ctext_hover, cgui_hover}, */
	/* }; */

	/* for (int i = 0; i < 3; i++) */
	/*	if (fabs(text_guis[i].l - text_guis[i].r) < 0.3) */
	/*		return false; */

	/* return (fabs(cgui_orig - cgui_active) >= 0.1); */
	return true;
}


bool
tw_validate_theme(struct taiwins_theme *theme)
{
	return validate_theme_colors(theme) && validate_theme_font(theme);
}

size_t
tw_theme_extract_fonts(struct taiwins_theme *theme, char *fonts[MAX_FONTS])
{
	size_t fidx = 0;
	if (*theme->ascii_font)
		fonts[fidx++] = theme->ascii_font;

	if (*theme->cjk_font &&
	    strcmp(theme->cjk_font, theme->ascii_font))
		fonts[fidx++] = theme->cjk_font;

	if (*theme->icons_font &&
	    strcmp(theme->icons_font, theme->ascii_font) &&
	    strcmp(theme->icons_font, theme->cjk_font))
		fonts[fidx++] = theme->icons_font;

	return fidx;
}



//text color does not change for buttons and UIs.
//the changing color is edit color
//text-edit-curosr changes color to text-color and text

//button color can be a little different, so as the edit,
//edit is a little darker and button can be lighter,
//the slider, cursor they are all different.
//quite unpossible to have them be the same

const struct taiwins_theme taiwins_dark_theme = {
	.row_size = 16,
	.text_color = {
		.r = 210, .g = 210, .b = 210, .a = 255,
	},
	.text_active_color = {
		.r = 40, .g = 58, .b = 61, .a=255,
	},
	.window_color = {
		.r = 57, .g = 67, .b = 71, .a=215,
	},
	.border_color = {
		.r = 46, .g=46, .b=46, .a=255,
	},
	.slider_bg_color = {
		.r = 50, .g=58, .b=61, .a=255,
	},
	.combo_color = {
		.r = 50, .g=58, .b=61, .a=255,
	},
	.button = {
		{.r = 48, .g=83, .b=111, .a=255},
		{.r = 58, .g=93, .b=121, .a=255},
		{.r = 63, .g =98, .b=126, .a=255},
	},
	.toggle = {
		{.r = 50, .g = 58, .b = 61, .a = 255},
		{.r = 45, .g = 53, .b = 56, .a = 255},
		{.r = 48, .g = 83, .b = 111, .a = 255},
	},
	.select = {
		.normal = {.r = 57, .g = 67, .b=61, .a=255,},
		.active = {.r = 48, .g = 83, .b=111, .a=255},
	},
	.slider = {
		{.r = 48, .g = 83, .b = 111, .a=245},
		{.r = 53, .g = 88, .b = 116, .a=255},
		{.r = 58, .g = 93, .b = 121, .a=255},
	},
	.chart = {
		{.r = 50, .g = 58, .b=61, .a=255},
		{.r = 255, .a = 255},
		{.r = 48, .g = 83, .b=111, .a=255},
	},
};


const struct taiwins_theme taiwins_light_theme = {

};

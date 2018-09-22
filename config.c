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
	float ctext_orig = rgb2grayscale(&theme->text_color);
	float ctext_active = rgb2grayscale(&theme->text_active_color);
	float ctext_hover = rgb2grayscale(&theme->text_hover_acolor);

	float cgui_orig = rgb2grayscale(&theme->gui_color);
	float cgui_active = rgb2grayscale(&theme->gui_active_color);
	float cgui_hover = rgb2grayscale(&theme->gui_hover_color);

	struct itensity_pair {
		float l, r;
	};
	struct itensity_pair text_guis[3] = {
		{ctext_orig, cgui_orig},
		{ctext_active, cgui_active},
		{ctext_hover, cgui_hover},
	};

	for (int i = 0; i < 3; i++)
		if (fabs(text_guis[i].l - text_guis[i].r) < 0.3)
			return false;

	return (fabs(ctext_orig - ctext_active) >= 0.3) &&
		(fabs(cgui_orig - cgui_active) >= 0.3);
}


bool
taiwins_validate_theme(struct taiwins_theme *theme)
{
	return validate_theme_colors(theme) && validate_theme_font(theme);
}

#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <cairo/cairo.h>
#include <wayland-client.h>
#include <fontconfig/fontconfig.h>

cairo_format_t
translate_wl_shm_format(enum wl_shm_format format)
{
	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
		return CAIRO_FORMAT_ARGB32;
		break;
	case WL_SHM_FORMAT_RGB888:
		return CAIRO_FORMAT_RGB24;
		break;
	case WL_SHM_FORMAT_RGB565:
		return CAIRO_FORMAT_RGB16_565;
		break;
	case WL_SHM_FORMAT_RGBA8888:
		return CAIRO_FORMAT_INVALID;
		break;
	default:
		return CAIRO_FORMAT_INVALID;
	}
}

size_t
stride_of_wl_shm_format(enum wl_shm_format format)
{
	switch(format) {
	case WL_SHM_FORMAT_ARGB8888:
		return 4;
		break;
	case WL_SHM_FORMAT_RGB888:
		return 3;
		break;
	case WL_SHM_FORMAT_RGB565:
		return 2;
		break;
	case WL_SHM_FORMAT_RGBA8888:
		return 4;
		break;
	case WL_SHM_FORMAT_ABGR1555:
		return 2;
	default:
		return 0;
	}
}

/** load image to texture and free all the context */
unsigned char *
load_image(const char *path, const enum wl_shm_format wlformat,
	   int width, int height, unsigned char *data)
{
	cairo_format_t format = translate_wl_shm_format(wlformat);
	if (format == CAIRO_FORMAT_INVALID)
		goto err_format;
	cairo_t *memcr;
	cairo_surface_t *pngsurface = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(pngsurface) != CAIRO_STATUS_SUCCESS)
		goto err_loadimg;

	int stride = cairo_format_stride_for_width(format, width);
	cairo_surface_t *memsurface = cairo_image_surface_create_for_data(data, format, width, height, stride);
	memcr = cairo_create(memsurface);
	//lol, I need to scale before I set the source
	fprintf(stderr, "we got image with dimension %d %d\n",
		cairo_image_surface_get_width(pngsurface), cairo_image_surface_get_height(pngsurface));
	cairo_scale(memcr, (double)width / cairo_image_surface_get_width(pngsurface) ,
		    (double)height / cairo_image_surface_get_height(pngsurface));
	cairo_set_source_surface(memcr, pngsurface, 0, 0);
	cairo_paint(memcr);
	//TODO, free memory
	cairo_destroy(memcr);
	cairo_surface_destroy(pngsurface);
	cairo_surface_destroy(memsurface);
	return data;

err_loadimg:
	cairo_surface_destroy(pngsurface);
err_format:
	return NULL;
}


static bool
validate_theme_font(struct taiwins_theme *theme)
{
	FcInit();
	FcConfig *config = FcConfigGetCurrent();
	if (strlen(theme->font) == 0)
		return false;

	FcChar8 *file = NULL;
	const char *path;
	FcResult result;
	FcPattern *pat = FcNameParse((const FcChar8 *)theme->font);
	FcConfigSubstitute(config, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcPattern *font = FcFontMatch(config, pat, &result);
	if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch) {
		path = (const char *)file;
		assert(strlen(path) < 255);
		strcpy(theme->font, path);
	}
	FcPatternDestroy(font);
	FcPatternDestroy(pat);
	FcFini();
	if (strlen(theme->font) == 0)
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

/* this cause memory leak, find out why! */
int
tw_find_font_path(const char *font_name, char *path, size_t path_cap)
{
	int path_len = 0;
	const char *default_font = "Vera";
	font_name = (strlen(font_name) == 0) ? default_font : font_name;

	FcInit();
	FcConfig *config = FcConfigGetCurrent();
	FcChar8 *file = NULL;
	const char *searched_path;
	FcResult result;
	FcPattern *pat = FcNameParse((const FcChar8 *)font_name);
	//I suppose there is the
	FcConfigSubstitute(config, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcPattern *font = FcFontMatch(config, pat, &result);
	if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch) {
		searched_path = (const char *)file;
		path_len = strlen(searched_path);
		path_len = (path_len < path_cap -1) ? path_len : -1;
		if (path_len > 0)
			strcpy(path, searched_path);
		//return ed is a reference
		/* free(file); */
		FcPatternDestroy(pat);
		FcPatternDestroy(font);
	}
	FcFini();
	return path_len;
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

#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>


#if defined (__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#elif defined (__clang__)
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmaybe-uninitialized"
#endif

#define NK_IMPLEMENTATION
#include "nuklear.h"


#define NK_CR_FONT_KEY ((cairo_user_data_key_t *)1000)


struct nk_cairo_font {
	//we need have a text font and an icon font, both of them should be FT_fontface
	int size;
	float scale;
	FT_Face text_font;
	FT_Face icon_font;
	FT_Library ft_lib;
};


static void
nk_cairo_font_done(struct nk_cairo_font *user_font)
{
	FT_Done_Face(user_font->text_font);
	FT_Done_Face(user_font->icon_font);
	FT_Done_FreeType(user_font->ft_lib);

}

static inline bool
is_in_pua(nk_rune rune)
{
	return rune >= 0xE000 && rune <= 0xF8FF;
}

static FT_Vector
nk_cairo_calc_advance(int32_t this_glyph, int32_t last_glyph,
		      FT_Face this_face, FT_Face last_face)
{
	FT_Vector advance_vec = {0, 0};
	if (this_face == last_face)
		FT_Get_Kerning(this_face, last_glyph, this_glyph,
			       ft_kerning_default, &advance_vec);

	FT_Load_Glyph(this_face, this_glyph, FT_LOAD_DEFAULT);
	advance_vec.x += this_face->glyph->advance.x;
	advance_vec.y += this_face->glyph->advance.y;
	return advance_vec;
}

//we need a fast way to calculate text width and rest is just rendering glyphs,
//since we already know how to
static double
nk_cairo_text_width(struct nk_cairo_font *user_font, float height, const char *text,
		    int utf8_len)
{
	//we do not need height
	//since we do not need the generate all glyphs, we do not need the array
	//of it
	double width = 0.0;
	int len_decoded = 0;
	nk_rune ucs4_this, ucs4_last = 0;
	FT_Face curr_face, last_face = NULL;
	int32_t glyph_this, glyph_last = -1;

	while (len_decoded < utf8_len) {

		int num_bytes = nk_utf_decode(text + len_decoded,
					      &ucs4_this, utf8_len - len_decoded);
		len_decoded += num_bytes;
		curr_face = is_in_pua(ucs4_this) ?
			user_font->icon_font :
			user_font->text_font;
		glyph_this = FT_Get_Char_Index(curr_face, ucs4_this);
		FT_Vector real_advance = nk_cairo_calc_advance(glyph_this, glyph_last,
							       curr_face, last_face);

		width += real_advance.x / 64.0;

		ucs4_last = ucs4_this;
		glyph_last = glyph_this;
		last_face = curr_face;
	}
	return width;
}

static int
utf8_to_ucs4(nk_rune *runes, const int ucs4_len, const char *text, const int utf8_len)
{
	int len_decoded = 0;
	int i = 0;
	while (len_decoded < utf8_len && i < ucs4_len) {
		len_decoded += nk_utf_decode(text+len_decoded, &runes[i++], utf8_len - len_decoded);
	}
	return i;
}

static bool
nk_cairo_is_whitespace(nk_rune code) {
	bool ws;
	switch (code) {
	case ' ':
	case '\t':
	case '\v':
	case '\f':
	case '\r':
		ws = true;
		break;
	default:
		ws = false;
	}
	return ws;
}

static struct nk_vec2
nk_cairo_render_glyph(cairo_t *cr, FT_Face face, const nk_rune codepoint,
		      const int32_t glyph, const struct nk_vec2 *baseline)
{
	cairo_matrix_t matrix;
	FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT);
	struct nk_vec2 advance = { face->glyph->metrics.horiAdvance / 64.0, 0 };
	if (nk_cairo_is_whitespace(codepoint))
		return advance;

	FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	int w = bitmap->width;
	int h = bitmap->rows;
	//make glyph and pattern
	uint32_t pixels[w * h * 4];
	for (int i = 0; i < w * h; i++) {
		pixels[i] = bitmap->buffer[i] << 24;

	}
	//now make pattern out of it
	cairo_surface_t *gsurf =
		cairo_image_surface_create_for_data((unsigned char *)pixels, CAIRO_FORMAT_ARGB32, w, h,
						    cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w));
	cairo_pattern_t *pattern = cairo_pattern_create_for_surface(gsurf);
	cairo_matrix_init_identity(&matrix);
	cairo_matrix_scale(&matrix, w, h);
	cairo_pattern_set_matrix(pattern, &matrix);

	{
		//horibearingY is a postive value
		struct nk_vec2 render_point = *baseline;
		float ybearing = face->glyph->metrics.horiBearingY / 64.0;
		float xbearing = face->glyph->metrics.horiBearingX / 64.0;
		render_point.y -= ybearing;
		render_point.x += xbearing;

		cairo_save(cr);
		cairo_translate(cr, render_point.x, render_point.y);
		cairo_scale(cr, w, h);
		cairo_mask(cr, pattern);
		cairo_fill(cr);
		cairo_restore(cr);
	}

	cairo_pattern_destroy(pattern);
	cairo_surface_destroy(gsurf);

	return advance;
}


static void
nk_cairo_render_text(cairo_t *cr, const struct nk_command_text *t,
		     struct nk_cairo_font *font, const char *text, const int utf8_len)
{
	//convert the text to unicode
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	//since we render horizontally, we only need to care about horibearingX,
	//horibearingY, horiAdvance
	//About the Pixel size

	//the baseline is actually the EM size, this is rather confusing, when
	//you set the pixel size 16. It set the pixel size of 'M', but the 'M'
	//has only ascent, it is already 16. for character like G, it has
	//descent, but smaller ascent, so the actual size of string is the
	//height, which is `18.625`.
	struct nk_vec2 advance = nk_vec2(t->x, t->y+font->size);
	nk_rune ucs4_this, ucs4_last = 0;
	FT_Face curr_face, last_face = NULL;
	int32_t glyph_this, glyph_last = -1;


	nk_rune unicodes[utf8_len];
	int nglyphs = utf8_to_ucs4(unicodes, utf8_len, text, utf8_len);
	if (nglyphs == 0)
		return;

	for (int i = 0; i < nglyphs; i++) {
		FT_Vector kern_vec = {0, 0};
		ucs4_this = unicodes[i];
		curr_face = is_in_pua(ucs4_this) ? font->icon_font : font->text_font;
		glyph_this = FT_Get_Char_Index(curr_face, unicodes[i]);
		if (curr_face == last_face)
			FT_Get_Kerning(curr_face, glyph_last, glyph_this, FT_KERNING_DEFAULT, &kern_vec);
		//we want to jump over white space as well
		//kerning, so we may move back a bit
		advance.x += kern_vec.x/64.0;
		advance.y += kern_vec.y/64.0;

		//now do the rendering
		struct nk_vec2 glyph_advance =
			nk_cairo_render_glyph(cr, curr_face, ucs4_this,
					      glyph_this, &advance);
		advance.x += glyph_advance.x;
		advance.y += glyph_advance.y;

		//rolling forward the state
		ucs4_last = ucs4_this;
		glyph_last = glyph_this;
		last_face = curr_face;
	}
}


void
nk_cairo_font_init(struct nk_cairo_font *font, const char *text_font, const char *icon_font)
{
	int error;
	FT_Init_FreeType(&font->ft_lib);
	font->size = 0;
	int pixel_size = 16;
	font->size = pixel_size;

//	char font_path[256];
	const char *vera = "/usr/share/fonts/TTF/Vera.ttf";
	const char *fa_a = "/usr/share/fonts/TTF/fa-regular-400.ttf";

	//tw_find_font_path(text_font, font_path, 256);
	error = FT_New_Face(font->ft_lib, vera, 0,
			    &font->text_font);

	if (FT_HAS_KERNING(font->text_font))
	    fprintf(stderr, "dejavu sans has kerning\n");
	//for the icon font, we need to actually verify the font charset, but
	//lets skip it for now
	//tw_find_font_path(icon_font, font_path, 256);
	error = FT_New_Face(font->ft_lib, fa_a, 0,
			    &font->icon_font);
	assert(!error);
}


void
nk_cairo_font_set_size(struct nk_cairo_font *font, int pix_size, float scale)
{
	font->size = pix_size * scale;
	font->scale = scale;
	int error;
	error = FT_Set_Pixel_Sizes(font->text_font, 0, pix_size * scale);
	error = FT_Set_Pixel_Sizes(font->icon_font, 0, pix_size * scale);
	(void)error;

}

int main(int argc, char *argv[])
{
	struct nk_cairo_font font;
	nk_cairo_font_init(&font, NULL, NULL);
	nk_cairo_font_set_size(&font, 16, 1.0);

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1000, 1000);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);

	struct nk_command_text cmd;
	cmd.x = 100; cmd.y = 100;

	int unicodes[] = {'V', 'A', 0xf1c1, 'O', 0xf1c2, 0xf1c3, 0xf1c4, 0xf1c5};
	char strings[256];
	int count = 0;
	for (int i = 0; i < 8; i++) {
		count += nk_utf_encode(unicodes[i], strings+count, 256-count);
	}
	strings[count] = '\0';

	nk_cairo_render_text(cr, &cmd, &font, strings, count);

	double text_width = nk_cairo_text_width(&font, 16, "abcdefghijkl", 12);
	fprintf(stderr, "the text width is %f\n", text_width);

	cairo_surface_write_to_png(surface, "/tmp/this_is_okay.png");

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	nk_cairo_font_done(&font);
	return 0;
}

#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#define NK_IMPLEMENTATION
#include "../3rdparties/nuklear/nuklear.h"


#define NK_CR_FONT_KEY ((cairo_user_data_key_t *)1000)


struct nk_cairo_font {
	//we need have a text font and an icon font, both of them should be FT_fontface
	cairo_font_face_t *font_face;
	cairo_scaled_font_t *font_using;
	int size;
	FT_Face text_font;
	FT_Face icon_font;
};




static void
nk_cairo_font_done(void *data)
{
	struct nk_cairo_font *user_font = (struct nk_cairo_font *)data;
	FT_Done_Face(user_font->text_font);
	FT_Done_Face(user_font->icon_font);
}

static inline bool is_in_pua(nk_rune rune)
{
	return rune >= 0xE000 && rune <= 0xF8FF;
}


static cairo_status_t
font_text_to_glyphs(
	cairo_font_face_t *font_face,
	const char *utf8,
	int utf8_len,
	cairo_glyph_t **glyphs,
	int *num_glyphs,
	cairo_text_cluster_t **clusters,
	int *num_clusters,
	cairo_text_cluster_flags_t *cluster_flags)
{
	struct nk_cairo_font *user_font =
		cairo_font_face_get_user_data(font_face, NK_CR_FONT_KEY);

	//get number of unicodes
	nk_rune unicodes[utf8_len];
	cairo_text_cluster_t max_clusters[utf8_len];
	int len_decoded = 0;
	int len = 0;

	while (len_decoded < utf8_len) {
		int num_bytes = nk_utf_decode(utf8 + len_decoded, unicodes + len,
					    utf8_len - len_decoded);
		len_decoded += num_bytes;
		max_clusters[len].num_bytes = num_bytes;
		max_clusters[len].num_glyphs = 1;
		len++;
	}
	//deal with clusters
	if (clusters) {
		*num_clusters = len;
		*clusters = cairo_text_cluster_allocate(len);
		memcpy(*clusters, max_clusters, len * sizeof(cairo_text_cluster_t));
	}

	*glyphs = cairo_glyph_allocate(len);
	*num_glyphs = len;

	//generate cairo_glyphs
	{

		FT_Face curr_face = is_in_pua(unicodes[0]) ?
			user_font->icon_font : user_font->text_font;
		FT_Face last_face;
		unsigned int glyph_idx = FT_Get_Char_Index(curr_face, unicodes[0]);
		FT_Load_Glyph(curr_face, glyph_idx, FT_LOAD_DEFAULT);
		//this is how we interpolate the font_type, we shift it by 1 bit,
		//the last bit to determine if it is a text font or icon font
		int font_type = is_in_pua(unicodes[0]) ? 1 : 0;
		(*glyphs)[0].x = 0.0;
		(*glyphs)[0].y = 0.0;
		(*glyphs)[0].index = (glyph_idx << 1) + font_type;
		FT_Vector advance = curr_face->glyph->advance;

		for (int i = 1; i < len; i++) {
			FT_Vector kern_vec = {0, 0};
			last_face = curr_face;
			curr_face = is_in_pua(unicodes[i]) ?
				user_font->icon_font : user_font->text_font;
			unsigned int last_glyph = glyph_idx;
			font_type = is_in_pua(unicodes[i]) ? 1 : 0;
			glyph_idx = FT_Get_Char_Index(curr_face, unicodes[i]);
			FT_Load_Glyph(curr_face, glyph_idx, FT_LOAD_DEFAULT);
			if (curr_face == last_face)
				FT_Get_Kerning(curr_face, last_glyph, glyph_idx, ft_kerning_default, &kern_vec);

			advance.x += kern_vec.x;
			advance.y += kern_vec.y;

			//get glyph and kerning
			(*glyphs)[i].x = i; //advance.x / (64.0 * user_font->size);
			(*glyphs)[i].y = 0.0;//advance.y / 64.0;
			(*glyphs)[i].index = (glyph_idx << 1) + font_type;

			advance.x += curr_face->glyph->advance.x;
			advance.y += curr_face->glyph->advance.y;
			fprintf(stderr, "the glyph %d is index:%lu, kern x: %f, kern y: %f\n", i,
				(*glyphs)[i].index, (*glyphs)[i].x, (*glyphs)[i].y);
		}
	}

	return CAIRO_STATUS_SUCCESS;
}

//text APIs
static cairo_status_t
scaled_font_text_to_glyphs(
	cairo_scaled_font_t *scaled_font,
	const char *utf8,
	int utf8_len,
	cairo_glyph_t **glyphs,
	int *num_glyphs,
	cairo_text_cluster_t **clusters,
	int *num_clusters,
	cairo_text_cluster_flags_t *cluster_flags)
{
	cairo_font_face_t *font_face = cairo_scaled_font_get_font_face(scaled_font);
	return font_text_to_glyphs(font_face, utf8, utf8_len, glyphs, num_glyphs,
				   clusters, num_clusters, cluster_flags);
}


static cairo_status_t
scaled_font_render_glyphs(cairo_scaled_font_t *scaled_font, unsigned long glyph,
			  cairo_t *cr, cairo_text_extents_t *extents)
{
	cairo_font_face_t *font_face = cairo_scaled_font_get_font_face(scaled_font);
	struct nk_cairo_font *user_font =
		cairo_font_face_get_user_data(font_face, NK_CR_FONT_KEY);

	FT_Face face = (glyph & 0x01) ?  user_font->icon_font : user_font->text_font;
	unsigned long real_glyph =  glyph >> 1;
	//now we render the glyph using The freetype
	int err = FT_Load_Glyph(face, real_glyph, FT_LOAD_DEFAULT);
	err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	//we only want 8 bit pixmap for the power of antialiasing
	assert(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);
	int width = bitmap->width;
	int height = bitmap->rows;
	int stride = bitmap->pitch;
	cairo_surface_t *glyph_surf =
		cairo_image_surface_create_for_data(bitmap->buffer,
						    CAIRO_FORMAT_A8, width, height, stride);
	cairo_pattern_t *pattern = cairo_pattern_create_for_surface(glyph_surf);
	cairo_set_source(cr, pattern);
	cairo_mask(cr, pattern);
	extents->x_advance = 1.0/1.6;
	extents->width = 1.0 / 1.6;
	cairo_pattern_destroy(pattern);
	cairo_surface_destroy(glyph_surf);
	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
scaled_font_init(cairo_scaled_font_t *scaled_font, cairo_t *cr, cairo_font_extents_t *extents)
{
	//I do not know what to do with it now, maybe I should get the
	//font_matrix, but what is that font matrix
	/* cairo_matrix_t mat; */
	/* cairo_scaled_font_get_font_matrix(scaled_font, &mat); */
	/* double font_size = mat.xx; */
	extents->ascent = 1.0;
	extents->descent = 0;
	return CAIRO_STATUS_SUCCESS;
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
	FT_Face curr_face, last_face;
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

static bool nk_cairo_is_whitespace(nk_rune code) {
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
		render_point.y -= ybearing;

		cairo_save(cr);
		cairo_translate(cr, render_point.x, render_point.y);
		cairo_scale(cr, w, h);
		cairo_rectangle(cr, 0, 0, 1, 1);
		cairo_mask(cr, pattern);
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
		fprintf(stderr, "the kerning for %c %c is (%ld, %ld)\n", ucs4_last, ucs4_this, kern_vec.x, kern_vec.y);

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
	FT_Library library;
	FT_Init_FreeType(&library);
	font->size = 0;
	int pixel_size = 16;
	font->size = pixel_size;

//	char font_path[256];
	const char *vera = "/usr/share/fonts/TTF/Vera.ttf";
	const char *fa_a = "/usr/share/fonts/TTF/fa-regular-400.ttf";

	//tw_find_font_path(text_font, font_path, 256);
	error = FT_New_Face(library, vera, 0,
			    &font->text_font);

	if (FT_HAS_KERNING(font->text_font))
	    fprintf(stderr, "dejavu sans has kerning\n");
	error = FT_Set_Pixel_Sizes(font->text_font, 0, pixel_size);
	//for the icon font, we need to actually verify the font charset, but
	//lets skip it for now
	//tw_find_font_path(icon_font, font_path, 256);
	error = FT_New_Face(library, fa_a, 0,
			    &font->icon_font);
	error = FT_Set_Pixel_Sizes(font->icon_font, 0, pixel_size);
	font->font_face = cairo_user_font_face_create();
	cairo_font_face_set_user_data(font->font_face, NK_CR_FONT_KEY, font,
				      nk_cairo_font_done);

	cairo_user_font_face_set_render_glyph_func(font->font_face,
						   scaled_font_render_glyphs);
	cairo_user_font_face_set_text_to_glyphs_func(font->font_face,
						     scaled_font_text_to_glyphs);
	cairo_user_font_face_set_init_func(font->font_face, scaled_font_init);

}


int main(int argc, char *argv[])
{
	struct nk_cairo_font font;
	nk_cairo_font_init(&font, NULL, NULL);

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1000, 1000);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);

	cairo_set_font_face(cr, font.font_face);
	struct nk_command_text cmd;
	cmd.x = 100; cmd.y = 100;
	nk_cairo_render_text(cr, &cmd, &font, "jikl", 4);

	double text_width = nk_cairo_text_width(&font, 16, "abcdefghijkl", 12);
	fprintf(stderr, "the text width is %f\n", text_width);

	cairo_surface_write_to_png(surface, "/tmp/this_is_okay.png");

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	cairo_font_face_destroy(font.font_face);
	return 0;
}

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
			(*glyphs)[i].x = advance.x / 64.0;
			(*glyphs)[i].y = advance.y / 64.0;
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
	cairo_mask(cr, pattern);
	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
nk_cairo_scale_font_init(cairo_scaled_font_t *scaled_font, cairo_t *cr, cairo_font_extents_t *extents)
{
	//I do not know what to do with it now, maybe I should get the
	//font_matrix, but what is that font matrix
	/* cairo_matrix_t mat; */
	/* cairo_scaled_font_get_font_matrix(scaled_font, &mat); */
	/* double font_size = mat.xx; */
	return CAIRO_STATUS_USER_FONT_NOT_IMPLEMENTED;
}

void
nk_cairo_font_init(struct nk_cairo_font *font, const char *text_font, const char *icon_font)
{
	int error;
	FT_Library library;
	FT_Init_FreeType(&library);
	font->size = 0;
	int pixel_size = 16;

//	char font_path[256];
	const char *vera = "/usr/share/fonts/TTF/Vera.ttf";
	const char *fa_a = "/usr/share/fonts/TTF/fa-regular-400.ttf";

	//tw_find_font_path(text_font, font_path, 256);
	error = FT_New_Face(library, vera, 0,
			    &font->text_font);
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

}

void sample_text(cairo_t *cr)
{
	cairo_set_font_size(cr, 16);
	cairo_scaled_font_t *scaled_font = cairo_get_scaled_font(cr);
	cairo_font_extents_t extents;

	const char *sample_str = "123456788";
	cairo_glyph_t *glyphs;
	int num_glyphs;
	int num_clusters;
	cairo_text_cluster_flags_t flags;
	cairo_text_cluster_t *clusters;

	cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
	cairo_scaled_font_extents(scaled_font, &extents);
	cairo_status_t status =
		cairo_scaled_font_text_to_glyphs(scaled_font, 100, 100+extents.ascent,
						 sample_str, 9, &glyphs, &num_glyphs,
						 &clusters, &num_clusters, &flags);
	cairo_show_text_glyphs(cr, sample_str, 9, glyphs, num_glyphs, clusters, num_clusters, flags);
	cairo_glyph_free(glyphs);
	cairo_text_cluster_free(clusters);
}


int main(int argc, char *argv[])
{
	struct nk_cairo_font font;
	nk_cairo_font_init(&font, NULL, NULL);

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1000, 1000);
	cairo_t *cr = cairo_create(surface);
	cairo_set_font_face(cr, font.font_face);

//	cairo_move_to(cr, 100, 100);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);

	/* cairo_move_to(cr, 100, 100); */
	sample_text(cr);


	cairo_surface_write_to_png(surface, "/tmp/this_is.png");

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	cairo_font_face_destroy(font.font_face);
	return 0;
}

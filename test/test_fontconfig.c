#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <fontconfig/fontconfig.h>
#include <cairo/cairo.h>

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY
#include "../3rdparties/nuklear/nuklear.h"

void print_pattern(FcPattern *pat)
{
	FcChar8 *s;
	double d;
	int i;

	if (FcPatternGetString(pat, FC_FAMILY, 0, &s) == FcResultMatch)
		fprintf(stdout, "family: %s\n", s);
	else
		fprintf(stdout, "no family.\n");

	if (FcPatternGetString(pat, FC_STYLE, 0, &s) == FcResultMatch)
		fprintf(stdout, "style: %s\n", s);
	else
		fprintf(stdout, "no style\n");
	if (FcPatternGetString(pat, FC_FULLNAME, 0, &s) == FcResultMatch)
		fprintf(stdout, "fullname: %s\n", s);
	else
		fprintf(stdout, "not fullname\n");
	if (FcPatternGetString(pat, FC_FILE, 0, &s) == FcResultMatch)
		fprintf(stdout, "file path:%s\n", s);
	else
		fprintf(stdout, "no file path\n");

	if (FcPatternGetDouble(pat, FC_DPI, 0, &d) == FcResultMatch)
		fprintf(stdout, "dpi: %f\n", d);
	else
		fprintf(stdout, "no dpi\n");
	/* if (FcPatternGetCharSet(pat, FC_CHARSET, 0, &cset) == FcResultMatch); */
	printf("\n");
}

static void
union_unicode_range(const nk_rune left[2], const nk_rune right[2], nk_rune out[2])
{
	nk_rune tmp[2];
	tmp[0] = left[0] < right[0] ? left[0] : right[0];
	tmp[1] = left[1] > right[1] ? left[1] : right[1];
	out[0] = tmp[0];
	out[1] = tmp[1];
}

//return true if left and right are insersected, else false
bool
intersect_unicode_range(const nk_rune left[2], const nk_rune right[2])
{
	return (left[0] <= right[1] && left[1] >= right[1]) ||
		(left[0] <= right[0] && left[1] >= right[0]);
}


static int
merge_unicode_range(const nk_rune *left, const nk_rune *right, nk_rune *out)
{
	//this is not sufficient, we need a union method
	int left_size = 0;
	while(*(left+left_size)) left_size++;
	int right_size = 0;
	while(*(right+right_size)) right_size++;

	nk_rune merged[left_size+right_size];
	int i = 0; int j = 0; int m = 0;
	merged[0] = (left[0] < right[0]) ? left[0] : right[0];
	merged[1] = (left[0] < right[0]) ? left[1] : right[1];
	//you need to get the next and be able to increase it
	while (i < (left_size/2) || j < (right_size/2)) {
		const nk_rune *next = (left[i*2] < right[j*2]) ? &left[i*2] : &right[j*2];
		int *next_id = (left[i*2] < right[j*2]) ? &i : &j;
		//if nothing is intersected, we have to move forward and copy
		//one to merged
		if (!intersect_unicode_range(&merged[m*2], next)) {
			m++;
			merged[2*m] = next[0];
			merged[2*m+1] = next[1];
			*next_id += 1;
		} else {
			//otherwise we do the merge
			union_unicode_range(next, &merged[m], &merged[m]);
			*next_id += 1;
		}
	}
	for (int i = 0; i< m; i++)
		fprintf(stderr, "(%x, %x),\n", merged[2*i], merged[2*i+1]);

	if (!out)
		return 2*m;
	memcpy(out, left, sizeof(nk_rune) * left_size);
	memcpy(out + left_size, right, sizeof(nk_rune) * right_size);
	*(out+left_size+right_size) = 0;
	return left_size + right_size;
}


static void
save_the_font_images(void)
{
	int w, h;
	struct nk_font_config cfg = nk_font_config(16);
	cfg.merge_mode = nk_true;
	cfg.range = nk_font_chinese_glyph_ranges();
	cfg.coord_type = NK_COORD_UV;

	struct nk_font_atlas atlas;
	nk_font_atlas_init_default(&atlas);
	nk_font_atlas_begin(&atlas);

	nk_font_atlas_add_from_file(&atlas,
				    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
				    16, 0);
	const void *data = nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_ALPHA8);
	printf("we had a texture of size %d, %d\n", w, h);
	//this may not be right...
	cairo_surface_t *image_surface =
		cairo_image_surface_create_for_data((unsigned char*)data,
						    CAIRO_FORMAT_A8,
						    w, h,
						    cairo_format_stride_for_width(CAIRO_FORMAT_A8, w));
	cairo_surface_write_to_png(image_surface, "/tmp/save_as_png.png");
	cairo_surface_destroy(image_surface);
}

int
main(int argc, char *argv[])
{
	FcInit();
	FcConfig* config = FcConfigGetCurrent();
	FcFontSet* sys_fonts = FcConfigGetFonts(config, FcSetSystem);
	FcFontSet* app_fonts = FcConfigGetFonts(config, FcSetApplication);
	//make pattern from font name
	/* FcPattern* pat = FcNameParse((const FcChar8*)"Font Awesome 5 Free"); */
	FcPattern* pat = FcNameParse((const FcChar8*)"DejaVu Sans");
	print_pattern(pat);

	FcConfigSubstitute(config, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	print_pattern(pat);

	char* fontFile; //this is what we'd return if this was a function
	// find the font
	FcResult result;
	FcPattern* font = FcFontMatch(config, pat, &result);
	print_pattern(font);

	if (font)
	{
		FcChar8* file = NULL;
		if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch)
		{
			//we found the font, now print it.
			//This might be a fallback font
			fontFile = (char*)file;
			printf("%s\n",fontFile);
		}
	}
	FcPatternDestroy(font);
	FcPatternDestroy(pat);
	/* FcFontSetDestroy(sys_fonts); */
	/* FcFontSetDestroy(app_fonts); */
	FcConfigDestroy(config);
	FcFini();
	save_the_font_images();
	const nk_rune* crange = nk_font_chinese_glyph_ranges();
	const nk_rune* krange = nk_font_korean_glyph_ranges();
	printf("%d\n", merge_unicode_range(crange, krange, NULL));
	return 0;
}

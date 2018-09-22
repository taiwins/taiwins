#include <stdio.h>
#include <fontconfig/fontconfig.h>

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


int main(int argc, char *argv[])
{
	FcInit();
	FcConfig* config = FcConfigGetCurrent();
	FcFontSet* sys_fonts = FcConfigGetFonts(config, FcSetSystem);
	FcFontSet* app_fonts = FcConfigGetFonts(config, FcSetApplication);
	//make pattern from font name
	FcPattern* pat = FcNameParse((const FcChar8*)"Font Awesome 5 Free");
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
	return 0;
}

#include <stdio.h>
#include <fontconfig/fontconfig.h>


int main(int argc, char *argv[])
{
	FcConfig* config = FcInitLoadConfigAndFonts();
	//make pattern from font name
	FcPattern* pat = FcNameParse((const FcChar8*)"Font Awesome 5 Brands");
	FcConfigSubstitute(config, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	char* fontFile; //this is what we'd return if this was a function
	// find the font
	FcResult result;
	FcPattern* font = FcFontMatch(config, pat, &result);
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
	FcConfigDestroy(config);
	return 0;
}

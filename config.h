/*******************************************************************
 *
 * this file servers the purpose of (default)unified configuration
 * shared among server and client
 *
 ******************************************************************/

#ifndef TW_CONFIG_H
#define TW_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <fontconfig/fontconfig.h>

#ifdef __cplusplus
extern "C" {
#endif
/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE 32
#define NUM_DECISIONS 500

struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));

struct tw_rgba_t {
	uint8_t r,g,b,a;
};

/* we need to be careful about what to put here */


/* the simpler version of style config, it is not gonna be as fancy as
 * nuklear(supporting nk_style_time(image or color) for all gui elements), but
 * it will have a consistent look
 */
struct taiwins_theme {
	uint32_t row_size; //this defines the text size as well
	struct tw_rgba_t text_color;
	struct tw_rgba_t gui_color;
	struct tw_rgba_t gui_active_color;
	struct tw_rgba_t hover_color;
	struct tw_rgba_t border_color;
	//these will be replaced by the font_file eventually
	char taiwins_ascii_font[256];
	char taiwins_icons_font[256];
	char taiwins_cjk_font[256];
};

bool taiwins_validate_theme(const struct taiwins_theme *);

#ifdef __cplusplus
}
#endif


#endif /* EOF */

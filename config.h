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
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-util.h>

#ifdef __cplusplus
extern "C" {
#endif
/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE 32
#define NUM_DECISIONS 500
#define MAX_PATH_LEN 256
#define MAX_FONTS 3

struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));

struct tw_rgba_t {
	uint8_t r,g,b,a;
};

/*****************************************************************/
/*                            theme                              */
/*****************************************************************/

/* the simpler version of style config, it is not gonna be as fancy as
 * nuklear(supporting nk_style_time(image or color) for all gui elements), but
 * it will have a consistent look
 */
struct taiwins_theme {
	uint32_t row_size; //this defines the text size as well
	struct tw_rgba_t text_color;
	struct tw_rgba_t text_active_color;
	struct tw_rgba_t text_hover_color;

	struct tw_rgba_t gui_color;
	struct tw_rgba_t gui_active_color;
	struct tw_rgba_t gui_hover_color;
	struct tw_rgba_t border_color;

	//we may need to extend this later
	char ascii_font[MAX_PATH_LEN];
	char icons_font[MAX_PATH_LEN];
	char cjk_font[MAX_PATH_LEN];
};


extern const struct taiwins_theme taiwins_dark_theme;
extern const struct taiwins_theme taiwins_light_theme;

/**
 * this function exams the theme(color and fonts are valid and do some convert)
 */
bool tw_validate_theme(struct taiwins_theme *);
size_t tw_theme_extract_fonts(struct taiwins_theme *, char *fonts[MAX_FONTS]);



static inline int
tw_font_pt2px(int pt_size, int ppi)
{
	if (ppi < 0)
		ppi = 96;
	return (int) (ppi / 72.0 * pt_size);
}

static inline int
tw_font_px2pt(int px_size, int ppi)
{
	if (ppi < 0)
		ppi = 96;
	return (int) (72.0 * px_size / ppi);
}



/*****************************************************************/
/*                           binding                             */
/*****************************************************************/
enum taiwins_modifier_mask {
	TW_NOMOD = 0,
	TW_ALT = 1,
	TW_CTRL = 2,
	TW_SUPER = 4,
	TW_SHIFT = 8,
};

struct taiwins_keypress {
	struct wl_list link;
	xkb_keycode_t keycode;
	enum taiwins_modifier_mask mod;
};

struct taiwins_btnpress {
	uint32_t btn;
	enum taiwins_modifier_mask mod;
};


struct taiwins_binding {
	union {
		struct taiwins_keypress keypress;
		struct taiwins_btnpress btnpress;
	};
	//we need a generalized run-binding, this can be later
};

static inline xkb_keycode_t
kc_linux2xkb(uint32_t kc_linux)
{
	//this should only work on x11, but very weird it works all the time
	return kc_linux+8;
}

static inline uint32_t
kc_xkb2linux(xkb_keycode_t kc_xkb)
{
	return kc_xkb-8;
}


static inline uint32_t
tw_mod_mask_from_xkb_state(struct xkb_state *state)
{
	uint32_t mask = TW_NOMOD;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_SUPER;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_SHIFT;
	return mask;
}


#ifdef __cplusplus
}
#endif


#endif /* EOF */

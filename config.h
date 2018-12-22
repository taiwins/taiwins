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


#ifdef __GNUC__
#define DEPRECATED(func) func __attribute__ ((deprecated))
#elif defined(_MSC_VER)
#define DEPRECATED(func) __declspec(deprecated) func
#else
#pragma message("WARNING: You need to implement DEPRECATED for this compiler")
#define DEPRECATED(func) func
#endif

struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));


/*****************************************************************/
/*                            theme                              */
/*****************************************************************/







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

//we need to think about this, because if we allow this, we need to have run-binding and
struct taiwins_binding {
	union {
		struct taiwins_keypress keypress;
		struct taiwins_btnpress btnpress;
	};
//	void () (struct taiwins_binding *b, )
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

/*******************************************************************
 *
 * this file servers the purpose of (default)unified configuration
 * shared among server and client
 *
 ******************************************************************/

#ifndef TW_SHARED_CONFIG_H
#define TW_SHARED_CONFIG_H


#include <stdint.h>
#include <stdbool.h>
#include <fontconfig/fontconfig.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-util.h>
#include <sequential.h>

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

/*****************************************************************/
/*                            theme                              */
/*****************************************************************/

//the definition should be moving to twclient

/*****************************************************************/
/*                           console                             */
/*****************************************************************/

struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));

/*****************************************************************/
/*                            shell                              */
/*****************************************************************/
#define TAIWINS_MAX_MENU_CMD_LEN 64
#define TAIWINS_MAX_MENU_ITEM_NAME 64

struct taiwins_menu_item {

	struct {
		char title[TAIWINS_MAX_MENU_ITEM_NAME];
		/* short commands. long commands please use console */
		char cmd[TAIWINS_MAX_MENU_CMD_LEN];
	} endnode;
	/* submenu settings */
	bool has_submenu; /* has submenu */
	size_t len; /* submenu size */
};

/* additional, we would have taiwins_menu_to_wl_array and
   taiwins_menu_from_wl_array */

struct taiwins_window_brief {
	float x,y,w,h;
	char name[32];
};

#ifdef __cplusplus
}
#endif


#endif /* EOF */

/*
 * shared_config.h - taiwins shared configuration for server and clients
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

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

struct tw_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));

/*****************************************************************/
/*                            shell                              */
/*****************************************************************/
#define TAIWINS_MAX_MENU_CMD_LEN 63
#define TAIWINS_MAX_MENU_ITEM_NAME 63

struct tw_menu_item {

	struct {
		char title[TAIWINS_MAX_MENU_ITEM_NAME+1];
		/* short commands. long commands please use console */
		char cmd[TAIWINS_MAX_MENU_CMD_LEN+1];
	} endnode;
	/* submenu settings */
	bool has_submenu; /* has submenu */
	size_t len; /* submenu size */
};

/* additional, we would have taiwins_menu_to_wl_array and
   taiwins_menu_from_wl_array */

struct tw_window_brief {
	float x,y,w,h;
	char name[32];
};

#ifdef __cplusplus
}
#endif


#endif /* EOF */

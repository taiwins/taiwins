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
#include <ctypes/sequential.h>
#include <ctypes/os/file.h>

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

struct tw_window_brief {
	float x,y,w,h;
	char name[32];
};

/*****************************************************************/
/*                           config                              */
/*****************************************************************/
static inline void
tw_config_dir(char config_home[PATH_MAX])
{
	char *xdg_cache = getenv("XDG_CONFIG_HOME");
	if (xdg_cache)
		sprintf(config_home, "%s/taiwins", xdg_cache);
	else
		sprintf(config_home, "%s/.config/taiwins", getenv("HOME"));

}

static inline bool
tw_create_config_dir(void)
{
	char config_home[PATH_MAX];
	mode_t config_mode = S_IRWXU | S_IRGRP | S_IXGRP |
		S_IROTH | S_IXOTH;
	tw_config_dir(config_home);
	if (mkdir_p(config_home, config_mode))
		return false;
	return true;
}

static inline void
tw_cache_dir(char cache_home[PATH_MAX])
{
	char *xdg_cache = getenv("XDG_CACHE_HOME");
	if (xdg_cache)
		sprintf(cache_home, "%s/taiwins", xdg_cache);
	else
		sprintf(cache_home, "%s/.cache/taiwins", getenv("HOME"));

}

static inline bool
tw_create_cache_dir(void)
{
	char cache_home[PATH_MAX];
	mode_t cache_mode = S_IRWXU | S_IRGRP | S_IXGRP |
		S_IROTH | S_IXOTH;
	tw_cache_dir(cache_home);
	if (mkdir_p(cache_home, cache_mode))
		return false;
	return true;
}

#ifdef __cplusplus
}
#endif


#endif /* EOF */

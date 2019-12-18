/*
 * common.h - taiwins client common functions
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

#ifndef TW_CLIENT_COMMON_H
#define TW_CLIENT_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>

#include <os/file.h>

/* helpers for clients */
#ifdef __cplusplus
extern "C" {
#endif


static inline void
taiwins_cache_dir(char cache_home[PATH_MAX])
{
	char *xdg_cache = getenv("XDG_CACHE_HOME");
	if (xdg_cache)
		sprintf(cache_home, "%s/taiwins", xdg_cache);
	else
		sprintf(cache_home, "%s/.cache/taiwins", getenv("HOME"));

}

static inline bool
create_cache_dir(void)
{
	char cache_home[PATH_MAX];
	mode_t cache_mode = S_IRWXU | S_IRGRP | S_IXGRP |
		S_IROTH | S_IXOTH;
	taiwins_cache_dir(cache_home);
	if (mkdir_p(cache_home, cache_mode))
		return false;
	return true;
}


#ifdef __cplusplus
extern "C" {
#endif

#endif /* EOF */

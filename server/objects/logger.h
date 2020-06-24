/*
 * logger.h - taiwins logging functions
 *
 * Copyright (c) 2020 Xichen Zhou
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

#ifndef TW_LOGGER_H
#define TW_LOGGER_H

#include <stdarg.h>
#include <stdio.h>

#ifdef  __cplusplus
extern "C" {
#endif


void
tw_logger_open(const char *path);

void
tw_logger_use_file(FILE *file);

void
tw_logger_close(void);

/**
 * @brief logging at info level.
 */
int
tw_log(const char *format, va_list args);

static inline int
tw_logl(const char *format, ...)
{
	int ret;
	va_list ap;
	va_start(ap, format);
	ret = tw_log(format, ap);
	va_end(ap);

	return ret;
}


#ifdef  __cplusplus
}
#endif

#endif /* EOF */

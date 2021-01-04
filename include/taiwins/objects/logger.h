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

enum TW_LOG_LEVEL {
	TW_LOG_INFO = 0,
	TW_LOG_DBUG = 1,
	TW_LOG_WARN = 2,
	TW_LOG_ERRO = 3,
};

void
tw_logger_open(const char *path);

void
tw_logger_use_file(FILE *file);

void
tw_logger_close(void);

int
tw_logv_level(enum TW_LOG_LEVEL level, const char *format, va_list ap);

/**
 * @brief logging at info level.
 */
static inline int
tw_log_level(enum TW_LOG_LEVEL level, const char *format, ...)
{
	int ret = 0;
	va_list args;

	va_start(args, format);
	ret = tw_logv_level(level, format, args);
	va_end(args);
	return ret;
}

#define tw_logl(format, ...) \
	tw_log_level(TW_LOG_INFO, "%s:%d " format, __FILE__, __LINE__, ##__VA_ARGS__)


#define tw_logl_level(level, format, ...) \
	tw_log_level(level, "%s:%d " format, __FILE__, __LINE__, \
	             ##__VA_ARGS__)

#ifdef  __cplusplus
}
#endif

#endif /* EOF */

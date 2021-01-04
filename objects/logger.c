/*
 * logger.c - taiwins logging functions
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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <taiwins/objects/logger.h>
#include <wayland-util.h>

static FILE *tw_logfile = NULL;

static const char *log_headers[] = {
	[TW_LOG_INFO] = "INFO: ",
	[TW_LOG_DBUG] = "DBUG: ",
	[TW_LOG_WARN] = "WARN: ",
	[TW_LOG_ERRO] = "ERRO: ",
};

static const char *log_colors[] = {
	[TW_LOG_INFO] = "\x1B[1;37m",
	[TW_LOG_DBUG] = "\x1B[1;35m",
	[TW_LOG_WARN] = "\x1B[1;33m",
	[TW_LOG_ERRO] = "\x1B[1;31m",
};

WL_EXPORT void
tw_logger_open(const char *path)
{
	if (tw_logfile && tw_logfile != stdout && tw_logfile != stderr)
		fclose(tw_logfile);
	tw_logfile = fopen(path, "w");
}

WL_EXPORT void
tw_logger_close(void)
{
	if (tw_logfile && tw_logfile != stdout && tw_logfile != stderr)
		fclose(tw_logfile);
}

WL_EXPORT void
tw_logger_use_file(FILE *file)
{
	if (!file)
		return;
	if (tw_logfile && tw_logfile != stdout && tw_logfile != stderr)
		fclose(tw_logfile);
	tw_logfile = file;
}

WL_EXPORT int
tw_logv_level(enum TW_LOG_LEVEL level, const char *format, va_list ap)
{
	int ret = -1;

	assert(level < TW_LOG_ERRO);
	if (tw_logfile) {
		bool color_log = isatty(fileno(tw_logfile));

		fprintf(tw_logfile, "%s", color_log ?
		        log_colors[level] : log_headers[level]);

		ret = vfprintf(tw_logfile, format, ap);
		fprintf(tw_logfile, "%s\n",  color_log ? "\x1B[0m" : "");

	}
	return ret;
}

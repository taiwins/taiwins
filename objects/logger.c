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
#include <stdio.h>
#include <taiwins/objects/logger.h>
#include <wayland-util.h>

static FILE *tw_logfile = NULL;

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

static const char *
level_to_string(enum TW_LOG_LEVEL level)
{
	switch (level) {
	case TW_LOG_INFO: return "INFO";
		break;
	case TW_LOG_DBUG: return "DBUG";
		break;
	case TW_LOG_WARN: return "WARN";
		break;
	case TW_LOG_ERRO: return "EE";
		break;
	}
	return "";
}

//TODO: if we can disable logger at release
WL_EXPORT int
tw_logv_level(enum TW_LOG_LEVEL level, const char *format, va_list ap)
{
	int ret = -1;

	assert(level < TW_LOG_ERRO);
	if (tw_logfile) {
		fprintf(tw_logfile, "%s: ", level_to_string(level));

		ret = vfprintf(tw_logfile, format, ap);
		fprintf(tw_logfile, "\n");
	}
	return ret;
}

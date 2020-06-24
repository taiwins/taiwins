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

#include "logger.h"
#include <stdio.h>

static FILE *tw_logfile = NULL;

int
tw_log(const char *format, va_list args)
{
	if (tw_logfile)
		return vfprintf(tw_logfile, format, args);
	return -1;
}

void
tw_logger_open(const char *path)
{
	if (tw_logfile && tw_logfile != stdout && tw_logfile != stderr)
		fclose(tw_logfile);
	tw_logfile = fopen(path, "w");
}

void
tw_logger_close(void)
{
	if (tw_logfile && tw_logfile != stdout && tw_logfile != stderr)
		fclose(tw_logfile);
}

void
tw_logger_use_file(FILE *file)
{
	if (!file)
		return;
	if (tw_logfile && tw_logfile != stdout && tw_logfile != stderr)
		fclose(tw_logfile);
	tw_logfile = file;
}

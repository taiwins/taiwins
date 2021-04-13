/*
 * profiler.c - taiwins server profiler handler
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <wayland-server-core.h>

#include <taiwins/objects/profiler.h>

#define THREAD_ID() (unsigned int)pthread_self()

#define MAX_SCOPES 10

struct tw_profiler_scope
{
	const char *name;
	long long start_time;
	long long end_time;
	unsigned int tid;
};

static struct tw_profiler {
	struct wl_display *display;
	struct wl_listener display_destroy;
	FILE *file;

	bool empty;
	int32_t depth;
	struct tw_profiler_scope scopes[MAX_SCOPES];

} s_profiler = {0};

static inline void
write_header(FILE *file)
{
	fprintf(file, "{\"otherData\": {},\"traceEvents\":[\n");
	fflush(file);
}

static inline void
write_footer(FILE *file)
{
	fprintf(file, "]}\n");
	fflush(file);
}

static void
write_start(const struct tw_profiler_scope *scope, FILE *file, bool comma)
{
	if (comma)
		fprintf(file, ",");
	fprintf(file, "{");
	fprintf(file, "\"cat\":\"function\",");
	fprintf(file, "\"name\":\"%s\",", scope->name);
	fprintf(file, "\"ph\":\"B\",");
	fprintf(file, "\"pid\":0,");
	fprintf(file, "\"tid\":%2u,", scope->tid);
	fprintf(file, "\"ts\":%lld", scope->start_time);
	fprintf(file, "}\n");
}

static void
write_end(const struct tw_profiler_scope *scope, FILE *file, bool comma)
{
	if (comma)
		fprintf(file, ",");
	fprintf(file, "{");
	fprintf(file, "\"cat\":\"function\",");
	fprintf(file, "\"name\":\"%s\",", scope->name);
	fprintf(file, "\"ph\":\"E\",");
	fprintf(file, "\"pid\":0,");
	fprintf(file, "\"tid\":%2u,", scope->tid);
	fprintf(file, "\"ts\":%lld", scope->end_time);
	fprintf(file, "}\n");
}

WL_EXPORT void
tw_profiler_close()
{
	write_footer(s_profiler.file);
	if (s_profiler.file != stdout && s_profiler.file != stderr)
		fclose(s_profiler.file);
	s_profiler.file = NULL;
}

static void
notify_profile_display_destroy(struct wl_listener *listener, void *data)
{
	tw_profiler_close();
	wl_list_remove(&s_profiler.display_destroy.link);
	s_profiler.display = NULL;
}

WL_EXPORT bool
tw_profiler_open(struct wl_display *display, const char *fname)
{
	FILE *file = fopen(fname, "w");
	if (!file)
		return false;
	if (s_profiler.file && s_profiler.file != stdout &&
	    s_profiler.file != stderr)
		fclose(s_profiler.file);

	s_profiler.file = file;
	s_profiler.empty = true;
	s_profiler.display = display;

	if (!s_profiler.display) {
		s_profiler.display = display;
		s_profiler.display_destroy.notify =
			notify_profile_display_destroy;
		wl_list_init(&s_profiler.display_destroy.link);
		wl_display_add_destroy_listener(display,
		                                &s_profiler.display_destroy);
	}
	write_header(file);
	return true;
}

WL_EXPORT void
tw_profiler_start_timer(const char *name)
{
	struct timespec spec;
	struct tw_profiler_scope *scope;

        assert(s_profiler.depth < MAX_SCOPES);
        scope = &s_profiler.scopes[s_profiler.depth];
        scope->name = name;
        scope->tid = THREAD_ID();
	clock_gettime(CLOCK_MONOTONIC, &spec);
	scope->start_time = (spec.tv_sec*1000000 + spec.tv_nsec/1000);
	write_start(scope, s_profiler.file, !s_profiler.empty);
	s_profiler.empty = false;
	s_profiler.depth+=1;
}

WL_EXPORT void
tw_profiler_stop_timer(const char *name)
{
	int32_t depth = s_profiler.depth-1;
	struct tw_profiler_scope *scope;
	struct timespec spec;

	assert(depth >= 0);
	assert(s_profiler.scopes[depth].name == name);
	s_profiler.depth-=1;
	scope = &s_profiler.scopes[depth];
	clock_gettime(CLOCK_MONOTONIC, &spec);
	scope->end_time = (spec.tv_sec*1000000 + spec.tv_nsec/1000);
	write_end(scope, s_profiler.file, !s_profiler.empty);
	s_profiler.empty = false;
}

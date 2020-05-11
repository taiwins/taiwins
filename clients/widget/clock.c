/*
 * battery.c - taiwins client clock widget
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

#include <stdlib.h>
#include <time.h>
#include <ctypes/helpers.h>
#include "widget.h"

static const char *MONTHS[] = {
	"January", "Feburary", "March", "April",
	"May", "June", "July", "August",
	"September", "October", "November", "December",
};

static const char *WEEKDAYS[] = {
	"S", "M", "T", "W", "T", "F", "S",
};

static struct clock_user_data_t {
	struct tm rtm;
	struct tm fdotm; //first day of the month
	struct tm fdonm; //first day of next month
} clock_user_data;

/*
 * This is a simple illustration of how to work with a row. We may really does
 * not know how much space the widget occupies inadvance.
 *
 * the widget should start with nk_layout_row_push to occupy its space, we limit
 * one widget right now, as it is what most time is. But we may need to have
 * more versatile approach
 */
static int
clock_widget_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	static const char * daysoftheweek[] =
		{"sun", "mon", "tus", "wed", "thu", "fri", "sat"};
	//it seems to work, we still need a library to figure out the text size
	/* nk_layout_row_push(ctx, 100); */
	time_t epochs = time(NULL);
	struct tm *tim = localtime(&epochs);

	return snprintf(label->label, 255, "%s %02d:%02d:%02d",
		       daysoftheweek[tim->tm_wday], tim->tm_hour, tim->tm_min, tim->tm_sec);
}

static inline struct tm
reduce_a_month(const struct tm *date)
{
	struct tm time = *date;
	//deal with 1900
	time.tm_year -= (time.tm_year > 0 && time.tm_mon == 0) ?
		1 : 0;
	time.tm_mon = (time.tm_mon == 0 && time.tm_year == 0) ? 0 :
		(time.tm_mon + 12 - 1) % 12;
	time_t epochs = mktime(&time);
	localtime_r(&epochs, &time);
	return time;
}

static inline struct tm
forward_a_month(const struct tm *date)
{
	struct tm time = *date;
	time.tm_year += (time.tm_mon == 11) ? 1 : 0;
	time.tm_mon = (time.tm_mon + 1) % 12;
	time_t epochs = mktime(&time);
	localtime_r(&epochs, &time);
	return time;
}

static inline int
days_in_a_year(int year)
{
	year += 1900;
	return (year % 400 == 0) ||
		(year % 4 == 0 && year % 100 != 0) ?
		366 : 365;
}

static void
calendar(struct nk_context *ctx, float width, float height, struct tw_appsurf *app)
{
	struct shell_widget *wig =
		container_of(app, struct shell_widget, widget);
	struct clock_user_data_t *ut = wig->user_data;
	char year[12], day[12]; //ensure enough size
	struct tm tm;
	time_t epochs = time(NULL);
	localtime_r(&epochs, &tm);

	float mratio[] = {0.1, 0.5, 0.3, 0.1};

	nk_layout_row(ctx, NK_DYNAMIC, 30, 4, mratio);
	if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_LEFT)) {
		ut->fdotm = reduce_a_month(&ut->fdotm);
		ut->fdonm = reduce_a_month(&ut->fdonm);
	}
	sprintf(year, "%4d", 1900+ut->fdotm.tm_year);
	nk_label(ctx, MONTHS[ut->fdotm.tm_mon], NK_TEXT_LEFT);
	nk_label(ctx, year, NK_TEXT_RIGHT);
	if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_RIGHT)) {
		ut->fdotm = forward_a_month(&ut->fdotm);
		ut->fdonm = forward_a_month(&ut->fdonm);
	}
	int ndays_mon = ut->fdonm.tm_yday - ut->fdotm.tm_yday;
	ndays_mon += (ndays_mon < 0) ?
		days_in_a_year(ut->fdotm.tm_year) : 0;

	nk_layout_row_dynamic(ctx, 20, 7);
	for (int i = 0; i < 7; i++)
		nk_label(ctx, WEEKDAYS[i], NK_TEXT_CENTERED);

	int pos = ut->fdotm.tm_yday - ut->fdotm.tm_wday;
	for (int i = 0; i < 5; i++) {
		nk_layout_row_dynamic(ctx, 20, 7);
		for (int j = 0; j < 7; j++) {
			sprintf(day, "%d", pos-ut->fdotm.tm_yday+1);
			if (pos < ut->fdotm.tm_yday ||
			    pos >= ut->fdotm.tm_yday + ndays_mon)
				nk_label(ctx, " ", NK_TEXT_CENTERED);
			else if (pos == tm.tm_yday)
				nk_label(ctx, day, NK_TEXT_CENTERED);
			else
				nk_label_colored(ctx, day, NK_TEXT_CENTERED,
						 nk_rgb_f(0.5, 0.5, 0.5));
			pos += 1;
		}
	}
}

static int
clock_setup(struct shell_widget *widget)
{
	struct clock_user_data_t *ut = widget->user_data;
	time_t epochs = time(NULL);
	localtime_r(&epochs, &ut->rtm);
	//setup first day of the month
	ut->fdotm = ut->rtm;
	ut->fdotm.tm_mday = 1;
	time_t t_fdotm = mktime(&ut->fdotm);
	localtime_r(&t_fdotm, &ut->fdotm);
	//and first day of next month
	ut->fdonm = ut->rtm;
	ut->fdonm.tm_mday = 1;
	ut->fdonm.tm_mon = (ut->rtm.tm_mon + 1) % 11;
	ut->fdonm.tm_year += (ut->fdonm.tm_mon == 0) ? 1 : 0;
	time_t t_fdonm = mktime(&ut->fdonm);
	localtime_r(&t_fdonm, &ut->fdonm);
	return 0;
}


struct shell_widget clock_widget = {
	.ancre_cb = clock_widget_anchor,
	.setup_cb = clock_setup,
	.draw_cb = calendar,
	.w = 200,
	.h = 200,
	.interval = {
		.it_value = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
		.it_interval = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
	},
	.file_path = NULL,
	.user_data = &clock_user_data,
};

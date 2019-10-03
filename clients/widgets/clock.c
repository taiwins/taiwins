#include "../../3rdparties/iconheader/IconsFontAwesome5_c.h"
#include "../widget.h"
#include <stdlib.h>
#include <time.h>

static const char *MONTHS[] = {
	"January", "Feburary", "March", "April",
	"May", "June", "July", "August",
	"September", "October", "November", "December",
};

static const char *WEEKDAYS[] = {
	"S", "M", "T", "W", "T", "F", "S",
};


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

static void
calendar(struct nk_context *ctx, float width, float height, struct app_surface *app)
{
	static time_t now = 0;
	struct tm tm, fdotm, fdonm; //first day of the month, first day of the next month
	char year[10], day[3];

	//use mktime to generate new data, it ignores tm_wday, tm_yday,
	if (!now)
		now = time(NULL);

	localtime_r(&now, &tm);
	fdotm = tm;
	fdotm.tm_mday = 1;
	time_t t_fdotm = mktime(&fdotm);
	localtime_r(&t_fdotm, &fdotm);

	fdonm = tm;
	fdonm.tm_mday = 1;
	fdonm.tm_mon = (tm.tm_mon + 1) % 11;
	fdonm.tm_year += (fdonm.tm_mon == 0) ? 1 : 0;
	time_t t_fdonm = mktime(&fdonm);
	localtime_r(&t_fdonm, &fdonm);
	size_t ndays_mon = fdonm.tm_yday - fdotm.tm_yday;
	ndays_mon += (ndays_mon < 0) ? 365 : 0;
	float mratio[] = {0.7, 0.3};

	sprintf(year, "%4d", 1900+tm.tm_year);
	nk_layout_row(ctx, NK_DYNAMIC, 30, 2, mratio);
	nk_label(ctx, MONTHS[tm.tm_mon], NK_TEXT_LEFT);
	nk_label(ctx, year, NK_TEXT_RIGHT);

	nk_layout_row_dynamic(ctx, 20, 7);
	for (int i = 0; i < 7; i++)
		nk_label(ctx, WEEKDAYS[i], NK_TEXT_CENTERED);

	int pos = fdotm.tm_yday - fdotm.tm_wday;
	for (int i = 0; i < 5; i++) {
		nk_layout_row_dynamic(ctx, 20, 7);
		for (int j = 0; j < 7; j++) {
			sprintf(day, "%d", pos-fdotm.tm_yday+1);
			if (pos < fdotm.tm_yday || pos >= fdonm.tm_yday)
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

struct shell_widget clock_widget = {
	.ancre_cb = clock_widget_anchor,
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
};

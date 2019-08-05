#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <os/file.h>
#include "../widget.h"
#include <IconsFontAwesome5_c.h>


//256 should be more than enough
struct battery_widget_data {
	char energy_now[256];
	char energy_full[256];
	char charging[256];
} battery_sys_files = {
	.energy_now = "\0",
	.energy_full = "\0",
	.charging = "\0",
};

static int
battery_anchor(struct shell_widget *widget, struct shell_widget_label *label)
{
	//this is the current design
	const char *energy_full = battery_sys_files.energy_full;
	const char *energy_now = battery_sys_files.energy_now;
	const char *online = battery_sys_files.charging;

	char energies[256]; int ef = 0, en = 0, ol = 0;
	if (!strlen(energy_now))
		ol = 1;
	else {
		file_read(energy_full, energies, 256);
		ef = atoi((const char *)energies);
		file_read(energy_now, energies, 256);
		en = atoi((const char *)energies);
		file_read(online, energies, 256);
		ol = atoi((const char *)energies);
	}

	float percent = (float)en / (float)ef;

	if (ol == 1)
		strncpy(label->label, ICON_FA_CHARGING_STATION, 256);
	else if (percent <= 0.1)
		strncpy(label->label, ICON_FA_BATTERY_EMPTY, 256);
	else if (percent > 0.1 && percent <= 0.3)
		strncpy(label->label, ICON_FA_BATTERY_QUARTER, 256);
	else if (percent > 0.3 && percent <= 0.6)
		strncpy(label->label, ICON_FA_BATTERY_HALF, 256);
	else if (percent > 0.6 && percent <= 0.8)
		strncpy(label->label, ICON_FA_BATTERY_THREE_QUARTERS, 256);
	else
		strncpy(label->label, ICON_FA_BATTERY_FULL, 256);
	return strlen(label->label);
}

//find your battery!
static int
battery_sysfile_find(struct shell_widget *widget, char *path)
{
	char batt_file[128];
	char adp_file[128];
	const char *power_supply = "/sys/class/power_supply";
	int bat_no = -1; int adp_no = -1;
	struct dirent *batt;
	DIR *dir = opendir(power_supply);
	if (!dir || is_dir_empty(dir)) {
		closedir(dir);
		return 0;
	}
	batt = dir_find_pattern(dir, "BAT%d", &bat_no);
	if (!batt)
		return 0;
	strncpy(batt_file, batt->d_name, 128);
	seekdir(dir, 0);

	batt = dir_find_pattern(dir, "ADP%d", &adp_no);
	if (!batt)
		return 0;
	strncpy(adp_file, batt->d_name, 128);
	closedir(dir);

	//we need to have additional checks
	if (bat_no == -1)
		return 0;
	sprintf(battery_sys_files.energy_now, "%s/%s/%s", power_supply, batt_file, "energy_now");
	sprintf(battery_sys_files.energy_full, "%s/%s/%s", power_supply, batt_file, "energy_full_design");
	if (adp_no != -1)
		sprintf(battery_sys_files.charging, "%s/%s/%s", power_supply, adp_file, "online");

	int total_len = strlen(battery_sys_files.energy_now);

	if (path)
		strcpy(path, battery_sys_files.energy_now);
	return total_len;
}


struct shell_widget battery_widget = {
	.ancre_cb = battery_anchor,
	.draw_cb = NULL,
	.w = 200,
	.h = 150,
	.path_find = battery_sysfile_find,
	.widget.s = 1,
	.user_data = &battery_sys_files,
	.interval = {{0}, {0}},
	.file_path = NULL,

};

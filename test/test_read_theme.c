#include "helpers.h"
#include "vector.h"
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <sequential.h>
#include <os/file.h>

enum icon_section_type {
	SEC_ICON_THEME,
	SEC_ICON_APP,
	SEC_ICON_MIMETYPES,
	SEC_ICON_PLACES,
	SEC_ICON_STATUS,
	SEC_ICON_DEVICES,
	SEC_ICON_UNKNOWN,
};

struct icon_theme {
	vector_t apps;
	vector_t mimes;
	vector_t places;
	vector_t status;
	vector_t devices;
};


static inline void
rtrim(char *line)
{
	char *ptr = line + strlen(line) - 1;
	while(ptr >= line && *ptr && isspace(*ptr)) *ptr-- = '\0';
}

static inline char *
ltrim(char *line)
{
	char *ptr = line;
	while (*ptr && isspace(*ptr)) ptr++;
	return ptr;
}

static inline char *
brace_remove(char *line)
{
	if (line[strlen(line)-1] == ']')
		line[strlen(line)-1] = '\0';
	if (*line == '[')
		return line+1;
	else
		return line;
}

static inline bool
is_section_name(const char *line)
{
	return (*line == '[' && line[strlen(line)-1] == ']');
}

static inline enum icon_section_type
get_section_type(const char *section)
{
	if (strcasestr(section, "icon theme"))
		return SEC_ICON_THEME;
	else if (strcasestr(section, "apps"))
		return SEC_ICON_APP;
	else if (strcasestr(section, "mime"))
		return SEC_ICON_MIMETYPES;
	else if (strcasestr(section, "places"))
		return SEC_ICON_PLACES;
	else if (strcasestr(section, "devices"))
		return SEC_ICON_DEVICES;
	else if (strcasestr(section, "status"))
		return SEC_ICON_STATUS;;
	return SEC_ICON_UNKNOWN;
}

void
search_icon_dir(char *path, int min_res, int max_res)
{
	char theme_file[1000];
	int allocated = 1000, len = 0;
	char *rawline = malloc(1000);
	char subdir[32] = {0};
	struct icon_theme theme;

	vector_init_zero(&theme.apps, 32, NULL);
	vector_init_zero(&theme.mimes, 32, NULL);
	vector_init_zero(&theme.places, 32, NULL);
	vector_init_zero(&theme.status, 32, NULL);
	vector_init_zero(&theme.devices, 32, NULL);

	DIR *dir = opendir(path);
	struct dirent *index = dir_find_pattern(dir, "index.theme");
	if (!index)
		goto out;
	//read the index
	strcpy(theme_file, path);
	path_concat(theme_file, 999, 1, index->d_name);
	if (!is_file_exist(theme_file))
		goto out;

	FILE *file = fopen(theme_file, "r");
	enum icon_section_type curr_section = SEC_ICON_UNKNOWN;

	while ((len = getline(&rawline, &allocated, file)) != -1) {
		char *line = ltrim(rawline); rtrim(line);
		char *comment_point = strstr(line, "#");
		if (comment_point)
			*comment_point = '\0';

		if (strlen(line) == 0)
			continue;
		if (is_section_name(line)) {
			curr_section = get_section_type(line);
			line = brace_remove(line);
			strcpy(subdir, line);
			continue;
		}
		if (curr_section != SEC_ICON_UNKNOWN)
			break;

		char *equal = NULL;
		equal = strstr(line, "=");
		if (equal)
			*equal = '\0';
		int curr_size = -1, minsize = 10000, maxsize = -1000;
		char *key = line;
		char *value = equal + 1;

		if (strcasecmp(key, "size") == 0) {
			curr_size = atoi(value);
			continue;
		} if (strcasecmp(key, "minsize") == 0) {
			minsize = atoi(value);
			continue;
		} if (strcasecmp(key, "maxsize") == 0) {
			maxsize = atoi(value);
			continue;
		}
		if (curr_section == SEC_ICON_APP &&
		    INBOUND(curr_size, min_res, max_res))
			strcpy(vector_newelem(&theme.apps), line);

		else if (curr_section == SEC_ICON_MIMETYPES &&
			INBOUND(curr_size, min_res, max_res))
			strcpy(vector_newelem(&theme.mimes), line);

		else if (curr_section == SEC_ICON_PLACES &&
			INBOUND(curr_size, min_res, max_res))
			strcpy(vector_newelem(&theme.places), line);

		else if (curr_section == SEC_ICON_DEVICES &&
			INBOUND(curr_size, min_res, max_res))
			strcpy(vector_newelem(&theme.devices), line);

		else if (curr_section == SEC_ICON_STATUS &&
			INBOUND(curr_size, min_res, max_res))
			strcpy(vector_newelem(&theme.status), line);

	}
	free(rawline);

out:
	closedir(dir);
}


int main(int argc, char *argv[])
{

	return 0;
}

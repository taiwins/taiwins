#include "helpers.h"
#include "vector.h"
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wayland-util.h>

#include <strops.h>
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

#define MAX_DIR_LEN 32

struct icon_dir {
	char dir[MAX_DIR_LEN];
	uint32_t size;
};


static void inline
copy_icon_dir(struct icon_dir *dir, const uint32_t size, const char *path)
{
	strcpy(dir->dir, path);
	dir->size = size;
}

struct icontheme_dir {
	vector_t apps;
	vector_t mimes;
	vector_t places;
	vector_t status;
	vector_t devices;
	char theme_dir[256];
};

static void
icontheme_dir_init(struct icontheme_dir *theme, const char *path)
{
	strcpy(theme->theme_dir, path);
	vector_init_zero(&theme->apps, sizeof(struct icon_dir), NULL);
	vector_init_zero(&theme->mimes, sizeof(struct icon_dir), NULL);
	vector_init_zero(&theme->places, sizeof(struct icon_dir), NULL);
	vector_init_zero(&theme->status, sizeof(struct icon_dir), NULL);
	vector_init_zero(&theme->devices, sizeof(struct icon_dir), NULL);
}

static void
icontheme_dir_release(struct icontheme_dir *theme)
{
	vector_destroy(&theme->apps);
	vector_destroy(&theme->mimes);
	vector_destroy(&theme->places);
	vector_destroy(&theme->status);
	vector_destroy(&theme->devices);
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
	if (strcmp(section, "[Icon Theme]") == 0)
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

struct size_state {
	char currdir[MAX_DIR_LEN];
	int curr_size, curr_max_size, curr_min_size;
	int curr_scale;
};

static inline bool
is_size_right(const int min_res, const int max_res,
	      const struct size_state *curr)
{
	bool inbound = INBOUND(curr->curr_size, min_res, max_res);
	if (curr->curr_max_size == -1 || curr->curr_max_size == -1)
		return inbound;
	bool minmax = curr->curr_min_size >= min_res &&
		curr->curr_max_size <= max_res;
	return inbound && minmax;
}

static inline void
size_state_reset(struct size_state *state, const char *path)
{
	strcpy(state->currdir, path);
	state->curr_max_size = -1;
	state->curr_min_size = -1;
	state->curr_size = -1;
	state->curr_scale = 1;
}

static int
cmp_icon_dir(const void *l, const void *r)
{
	const struct icon_dir *ld = l, *rd = r;
	return rd->size - ld->size;
}

void
search_icon_dirs(struct icontheme_dir *output,
	const char *path, const int min_res, const int max_res)
{
	struct size_state state = {{0}, -1, -1, -1, 1};
        char theme_file[1000];
	size_t allocated = 1000, len = 0;
	char *rawline = malloc(1000);
	DIR *dir = opendir(path);

	icontheme_dir_init(output, path);
	strcpy(theme_file, path);
	path_concat(theme_file, 999, 1, "index.theme");
	if (!is_file_exist(theme_file))
		goto out;

	FILE *file = fopen(theme_file, "r");
	enum icon_section_type curr_section = SEC_ICON_UNKNOWN;

	while ((len = getline(&rawline, &allocated, file)) != -1) {
		char *line = strop_ltrim(rawline); strop_rtrim(line);
		char *comment_point = strstr(line, "#");
		if (comment_point)
			*comment_point = '\0';

		if (strlen(line) == 0)
			continue;
		//get section
		if (is_section_name(line)) {
			curr_section = get_section_type(line);
			line = brace_remove(line);
			//skip over the dirs that too long
			if (strlen(line) >= MAX_DIR_LEN-1)
				curr_section = SEC_ICON_UNKNOWN;
			else
				size_state_reset(&state, line);
			continue;
		}
		if (curr_section == SEC_ICON_UNKNOWN)
			continue;
		//theme also has the inherits attributes, it complicates the
		//logics
		char *equal = NULL;
		equal = strstr(line, "=");
		if (equal)
			*equal = '\0';

		//deal with key value
		char *key = line;
		char *value = equal + 1;
		//size is a required key, so we are good
		if (strcasecmp(key, "size") == 0) {
			state.curr_size = state.curr_scale * atoi(value);
			continue;
		} else if (strcasecmp(key, "minsize") == 0) {
			state.curr_min_size = atoi(value);
			continue;
		} else if (strcasecmp(key, "maxsize") == 0) {
			state.curr_max_size = atoi(value);
			continue;
		} else if (strcasecmp(key, "scale") == 0) {
			state.curr_scale = atoi(value);
		}
		if (curr_section == SEC_ICON_APP &&
		    is_size_right(min_res, max_res, &state))
			copy_icon_dir(vector_newelem(&output->apps),
			              state.curr_size, state.currdir);

		else if (curr_section == SEC_ICON_MIMETYPES &&
		    is_size_right(min_res, max_res, &state))
			copy_icon_dir(vector_newelem(&output->mimes),
			              state.curr_size, state.currdir);

		else if (curr_section == SEC_ICON_PLACES &&
		    is_size_right(min_res, max_res, &state))
			copy_icon_dir(vector_newelem(&output->places),
			              state.curr_size, state.currdir);

		else if (curr_section == SEC_ICON_DEVICES &&
		    is_size_right(min_res, max_res, &state))
			copy_icon_dir(vector_newelem(&output->devices),
			              state.curr_size, state.currdir);

		else if (curr_section == SEC_ICON_STATUS &&
		    is_size_right(min_res, max_res, &state))
			copy_icon_dir(vector_newelem(&output->status),
			              state.curr_size, state.currdir);
	}
	free(rawline);

	qsort(output->apps.elems, output->apps.len, output->apps.elemsize,
	      cmp_icon_dir);
	qsort(output->mimes.elems, output->mimes.len, output->mimes.elemsize,
	      cmp_icon_dir);
	qsort(output->places.elems, output->places.len, output->places.elemsize,
	      cmp_icon_dir);
	qsort(output->devices.elems, output->devices.len, output->devices.elemsize,
	      cmp_icon_dir);
	qsort(output->status.elems, output->status.len, output->status.elemsize,
	      cmp_icon_dir);

out:
	closedir(dir);
}

static bool
filename_exists(const char *filename,
		const struct wl_array *handle_pool,
		const struct wl_array *string_pool)
{
	off_t *off;
	const char *ptr = string_pool->data;
	wl_array_for_each(off, handle_pool) {
		const char *path = ptr + *off;
		if (strstr(path, filename))
			return true;
	}
	return false;
}

static int
search_icon_subdir(const char *dir_path,
		struct wl_array *handle_pool,
		struct wl_array *string_pool)
{
	int count = 0;
	char filepath[1024];
	DIR *dir = opendir(dir_path);
	if (!dir)
		return 0;
	for (struct dirent *entry = readdir(dir); entry;
	     entry = readdir(dir)) {
		if (!(entry->d_type == DT_REG ||
		      entry->d_type == DT_LNK) ||
		    (!is_file_type(entry->d_name, ".svg") &&
		     !is_file_type(entry->d_name, ".png")))
			continue;
		if (filename_exists(entry->d_name, handle_pool, string_pool))
			continue;
		if (strlen(entry->d_name) + strlen(dir_path) + 1 >= 1024)
			continue;
		strcpy(filepath, dir_path);
		path_concat(filepath, 1024, 1, entry->d_name);
		char *tocpy = wl_array_add(string_pool, sizeof(filepath)+1);
		*(off_t *)wl_array_add(handle_pool, sizeof(off_t)) =
			(tocpy - (char *)string_pool->data);
		strcpy(tocpy, filepath);
		count++;
	}
	closedir(dir);
	return count;
}

void
search_icon_imgs(struct wl_array *handle_pool, struct wl_array *string_pool,
                 const char *themepath, const vector_t *icondir)
{
	//handles needs to be initialized
	char absdir[1024];
	struct icon_dir *icons = NULL;
	int count = 0;

	if (strlen(themepath) + MAX_DIR_LEN + 2 >= 1024)
		return;

	vector_for_each(icons, icondir) {
		strcpy(absdir, themepath);
		path_concat(absdir, sizeof(absdir), 1, icons->dir);
		count += search_icon_subdir(absdir, handle_pool, string_pool);
	}

}

int main(int argc, char *argv[])
{
	struct wl_array string_pool, handle_pool;
	struct icontheme_dir themedir;
	/* struct icon_dir *icon = NULL; */
	/* off_t *off = NULL; */

	search_icon_dirs(&themedir, "/usr/share/icons/Humanity", 31, 128);
	//print
	/* vector_for_each(icon, &themedir.apps) */
	/*	printf("%s\t", icon->dir); */
	/* printf("\n"); */
	/* vector_for_each(icon, &themedir.mimes) */
	/*	printf("%s\t", icon->dir); */
	/* printf("\n"); */
	/* vector_for_each(icon, &themedir.places) */
	/*	printf("%s\t", icon->dir); */
	/* printf("\n"); */
	/* vector_for_each(icon, &themedir.devices) */
	/*	printf("%s\t", icon->dir); */
	/* printf("\n"); */
	/* vector_for_each(icon, &themedir.status) */
	/*	printf("%s\t", icon->dir); */
	/* printf("\n"); */

	//doing search on
	vector_t *to_searches[] = {
		&themedir.apps, &themedir.mimes, &themedir.devices,
		&themedir.places, &themedir.status
	};
	for (int i = 0; i < NUMOF(to_searches); i++) {
		wl_array_init(&string_pool);
		wl_array_init(&handle_pool);
		search_icon_imgs(&handle_pool, &string_pool,
		                 "/usr/share/icons/Humanity", to_searches[i]);
		/* wl_array_for_each(off, &handle_pool) { */
		/*	fprintf(stdout, "%s\n", (char *)string_pool.data + *off); */
		/* } */

		wl_array_release(&string_pool);
		wl_array_release(&handle_pool);
	}



	icontheme_dir_release(&themedir);
	return 0;
}

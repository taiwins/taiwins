#include "vector.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>
#include <wayland-util.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>

#include <os/file.h>
#include <sequential.h>
#include <image_cache.h>

#include "common.h"

//the struct requires two parts, a image list with name and dimensions, and a
//tile image.

//it is the updater's responsibility to collect all the images and also quick
//hash directories(optional), the readers simply reads the cache and provides
//radix tree to do quick search, another problem here is that is has png/svg/xpm

// then you would have a big ass tile picture that loads everything for you.
// which we have a base resolution,
struct icon_cache_option {
	bool update_all;
	bool force_update;
	const char *update_theme;
	int high_res; //highest resolution to search
	int low_res; //lowest resolution to search
};

struct icon_cache_config {
	char path[256];
	int res;
};

static const char *usage =
	"usage: taiwins_update_icon_cache [OPTION...]\n"
	"\n"
	"Help option:\n"
	"  --help\t\t\tshow help\n"
	"\n"
	"Application options:\n"
	"  -f, --force\t\t\tIgnore the existing cache\n"
	"  -h, --hires\t\t\tSpecify the highest resolution to sample, maximum 256, default 128\n"
	"  -l, --lowres\t\t\tSpecify the lowest resolution to sample, minimum 32, default 32\n"
	"  -t, --theme\t\t\tSepcify the theme to update\n"
	"\n";

static const char *hicolor_path = "/usr/share/icons/hicolor";

//match those with name
enum icon_type {
	ICON_ACTIONS = 0,
	ICON_ANIMATIONS = 1,
	ICON_APPS = 2,
	ICON_CATEGORIES = 3,
	ICON_DEVICES = 4,
	ICON_MIMETYPES = 5,
	ICON_STATUS = 6,
};

//obtaining
static int
icon_size_cmp(const void *p1, const void *p2)
{
	int p1_resx, p1_resy, p2_resx, p2_resy;
	sscanf((const char *)p1, "%dx%d", &p1_resx, &p1_resy);
	sscanf((const char *)p2, "%dx%d", &p2_resx, &p2_resy);
	return p2_resx - p1_resx;
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
search_icon_dir(const char *dir_path,
		struct wl_array *handle_pool,
		struct wl_array *string_pool)
{
	int count = 0;
	DIR *dir = opendir(dir_path);

	if (!dir)
		return count;
	for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
		char file_path[1024];
		//TODO, take svg as well.
		if (!(entry->d_type == DT_REG ||
		      entry->d_type == DT_LNK) ||
		    !is_file_type(entry->d_name, ".png"))
			continue;
		if (filename_exists(entry->d_name, handle_pool, string_pool))
			continue;
		strcpy(file_path, dir_path);
		//find the same file in the string_pool
		path_concat(file_path, 1024, 1, entry->d_name);
		char *tocpy = wl_array_add(string_pool, sizeof(file_path)+1);
		*(off_t *)wl_array_add(handle_pool, sizeof(off_t)) =
			(tocpy - (char *)string_pool->data);
		strcpy(tocpy, file_path);
		count++;
	}
	closedir(dir);
	return count;
}

static int
search_theme(const struct icon_cache_config *config,
	     struct wl_array *handle_pool,
	     struct wl_array *str_pool)
{
	int count = 0;
	vector_t resolutions;
	vector_t hicolor_resolutions;
	typedef char dir_name_t[256];

	struct {
		vector_t *res;
		const char *path;
	} to_search[] = {
		{&resolutions, config->path},
		{&hicolor_resolutions, hicolor_path},
	};

	for (int i = 0; i < 2; i++) {
		DIR *dir = NULL;
		int resx, resy;

		vector_init_zero(to_search[i].res, 256, NULL);
		if (!to_search[i].path)
			continue;
		if (!(dir = opendir(to_search[i].path)))
			break;
		for(struct dirent *entry = readdir(dir);
		    entry; entry = readdir(dir)) {
			if (sscanf(entry->d_name, "%dx%d", &resx, &resy) == 2 &&
			    entry->d_type == DT_DIR &&
			    resx <= config->res) {
				char *path = vector_newelem(to_search[i].res);
				strncpy(path, entry->d_name, 256);
			}
		}
		closedir(dir);
		qsort(to_search[i].res->elems,
		      to_search[i].res->len,
		      to_search[i].res->elemsize,
		      icon_size_cmp);
	}
	if (!resolutions.len && !hicolor_resolutions.len)
		goto out;
	/* for (int i = 0; i < 2; i++) { */
	/*	dir_name_t *pos; */
	/*	vector_for_each(pos, to_search[i].res) */
	/*		fprintf(stderr, "%s\n", *pos); */
	/* } */

	for (int i = 0; i < 2; i++) {
		//search for some dirs and(right now just the app dir)
		dir_name_t *pos;
		char path[1024];
		vector_for_each(pos, to_search[i].res) {
			strcpy(path, to_search[i].path);
			path_concat(path, 1024, 2, pos, "apps");
			if (!is_dir_exist(path))
				continue;
			count += search_icon_dir(path, handle_pool, str_pool);
		}
	}
out:
	vector_destroy(&resolutions);
	vector_destroy(&hicolor_resolutions);
	return count;
}

//decided whether to search svgs, if we search for svgs, include rsvg is a necessary.

static void
path_to_node(char output[256], const char *input)
{
	char *copy = strdup(input);
	char *base = basename(copy);
	//remove png at the tail.
	strncpy(output, base, 255);
	free(copy);
}

#include <cairo.h>

static bool
cache_needs_update(const char *cache_file, const char *theme_path)
{
	struct stat cache_stat, theme_stat;
	if (!is_file_exist(cache_file))
		return true;
	stat(cache_file, &cache_stat);
	stat(theme_path, &theme_stat);
	return theme_stat.st_mtim.tv_sec >
		cache_stat.st_mtim.tv_sec;
}

static bool
parse_arguments(int argc, char *argv[],
		struct icon_cache_option *option)
{
	//defaults
	option->force_update = false;
	option->update_all = true;
	option->update_theme = NULL;
	option->high_res = 128;
	option->low_res = 32;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--help")) {
			fprintf(stderr, "%s", usage);
			return false;
		} else if (!strcmp(arg, "-f") || !strcmp(arg, "--force"))
			option->force_update = true;
		else if (!strcmp(arg, "--theme") && (i+1) < argc) {
			option->update_theme = argv[i+1];
			option->update_all = false;
			++i;
		} else if ((!strcmp(arg, "-h") || !strcmp(arg, "--hires")) &&
			   (i+1) < argc) {
			option->high_res = atoi(argv[i+1]);
			i++;
		} else if ((!strcmp(arg, "-l") || !strcmp(arg, "--lowres")) &&
			   (i+1) < argc) {
			option->low_res = atoi(argv[i+1]);
			i++;
		} else {
			fprintf(stderr, "%s", usage);
			return false;
		}
	}
	if (option->high_res <= option->low_res ||
	    option->high_res > 256 || option->low_res < 32)
		return false;
	return true;
}

int
main(int argc, char *argv[])
{
	int fd = -1;
	vector_t theme_lookups;
	struct icon_cache_option option;
	struct icon_cache_config theme, *current = NULL;
	char cache_home[PATH_MAX];

	if (!parse_arguments(argc, argv, &option))
		return -1;
	if (!create_cache_dir())
		return -1;
	vector_init_zero(&theme_lookups,
			 sizeof(struct icon_cache_config),
			 NULL);
	theme.res = option.high_res;
	if (option.update_theme) {
		sprintf(theme.path, "%s/%s", "/usr/share/icons", option.update_theme);
		if (!is_dir_exist(theme.path)) {
			fprintf(stderr, "theme %s not found", option.update_theme);
			return -1;
		}
		vector_append(&theme_lookups, &theme);
	} else { /* update all */
		DIR *dir = opendir("/usr/share/icons");
		for(struct dirent *entry = readdir(dir);
		    entry; entry = readdir(dir)) {
			strcpy(theme.path, "/usr/share/icons/");
			strcat(theme.path, entry->d_name);
			vector_append(&theme_lookups, &theme);
		}
		closedir(dir);
	}
	//now we just do the config
	vector_for_each(current, &theme_lookups) {
		char *name = basename(current->path);
		taiwins_cache_dir(cache_home);
		strcat(cache_home, "/");
		strcat(cache_home, name);
		strcat(cache_home, ".icon.cache");
		if (!option.force_update &&
		    !cache_needs_update(cache_home, current->path))
			continue;
		struct wl_array handles, strings;
		wl_array_init(&handles);
		wl_array_init(&strings);
		search_theme(current, &handles, &strings);
		struct image_cache cache =
			image_cache_from_arrays(&handles, &strings, path_to_node);
		int flags = is_file_exist(cache_home) ? O_RDWR : O_RDWR | O_CREAT;
		fd = open(cache_home, flags, S_IRUSR | S_IWUSR | S_IRGRP);
		image_cache_to_fd(&cache, fd);
		wl_array_release(&handles);
		wl_array_release(&strings);
		image_cache_release(&cache);
		close(fd);
	}
	vector_destroy(&theme_lookups);
	return 0;
}

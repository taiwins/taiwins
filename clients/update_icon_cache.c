#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>
#include <wayland-util.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <cairo.h>

#include <strops.h>
#include <os/file.h>
#include <sequential.h>
#include <image_cache.h>

#include "common.h"

struct icon_cache_option {
	bool update_all;
	bool force_update;
	const char *update_theme;
	int high_res; //highest resolution to search
	int low_res; //lowest resolution to search
	uint32_t update_list;
};

struct icon_cache_config {
	char path[256];
	int high_res;
	int low_res;
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
	"  -t, --theme\t\t\tSepcify the theme to update, default all\n"
	"  -u, --updates\t\t\tSpecify the update list in (apps,mimes,places,status,devices). default all\n"
	"\n";

static const char *hicolor_path = "/usr/share/icons/hicolor";

//match those with name
enum icon_type {
	ICON_APPS = 1,
	ICON_MIMETYPES = 2,
	ICON_PLACES = 4,
	ICON_STATUS = 8,
	ICON_DEVICES = 16,
	ICON_UNKNOWN = 100000,
};

static enum icon_type
icon_type_from_name(const char *name)
{
	if (!strcmp(name, "apps"))
		return ICON_APPS;
	else if (!strcmp(name, "mimes"))
		return ICON_MIMETYPES;
	else if (!strcmp(name, "places"))
		return ICON_PLACES;
	else if (!strcmp(name, "status"))
		return ICON_STATUS;
	else if (!strcmp(name, "devices"))
		return ICON_DEVICES;
	else
		return ICON_UNKNOWN;
}

static const char *
name_from_icon_type(uint32_t type)
{
	if (type == ICON_APPS)
		return ".apps.icon.cache";
	else if (type == ICON_MIMETYPES)
		return ".mimes.icon.cache";
	else if (type == ICON_PLACES)
		return ".places.icon.cache";
	else if (type == ICON_STATUS)
		return ".status.icon.cache";
	else if (type == ICON_DEVICES)
		return ".devices.icon.cache";
	return NULL;
}

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
	option->update_list = ICON_APPS | ICON_MIMETYPES |
		ICON_PLACES | ICON_STATUS | ICON_DEVICES;
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
		} else if ((!strcmp(arg, "-u") || !strcmp(arg, "--updates")) &&
		           (i+1) < argc) {
			option->update_list = 0;
			char *save_ptr = NULL, *elem = NULL;
			enum icon_type type = ICON_UNKNOWN;
			for (elem = strtok_r(argv[i+1], ",", &save_ptr);
			     elem; elem = strtok_r(NULL, ",", &save_ptr)) {
				if ((type = icon_type_from_name(elem)) ==
				    ICON_UNKNOWN)
					return false;
				option->update_list |= type;
			}
			if (!option->update_list) {
				fprintf(stderr, "%s", usage);
				return false;
			}

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

static bool
retrieve_themes_to_search(vector_t *theme_lookups,
                          const struct icon_cache_option *option)
{
	vector_init_zero(theme_lookups,
			 sizeof(struct icon_cache_config),
			 NULL);
	if (option->update_theme) {
		struct icon_cache_config theme_elem = {
			.high_res =  option->high_res,
			.low_res = option->low_res
		};

		path_concat(theme_elem.path, sizeof(theme_elem.path), 2,
		            "usr/share/icons", option->update_theme);
		if (!is_dir_exist(theme_elem.path)) {
			fprintf(stderr, "theme %s not found", option->update_theme);
			return false;
		}
		vector_append(theme_lookups, &theme_elem);
	} else {  //update all
		DIR *dir = opendir("/usr/share/icons");
		for(struct dirent *entry = readdir(dir);
		    entry; entry = readdir(dir)) {
			struct icon_cache_config theme_elem = {
				.high_res =  option->high_res,
				.low_res = option->low_res
			};

			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0)
				continue;
			path_concat(theme_elem.path, sizeof(theme_elem.path), 2,
			            "usr/share/icons", entry->d_name);
			vector_append(theme_lookups, &theme_elem);
		}
		closedir(dir);
	}
	return true;
}

static void
path_to_node(char output[256], const char *input)
{
	char *copy = strdup(input);
	char *base = basename(copy);
	*strrchr(base, '.') = 0;
	strop_ncpy(output, base, 256);
	free(copy);
}

static void
update_theme(const struct icon_cache_config *current,
             const struct icon_cache_option *option,
             const struct icontheme_dir *hicolor)
{
	char cache_file[PATH_MAX];
	char path[256];
	strcpy(path, current->path);
	char *name = basename(path);
	struct icontheme_dir theme;
	const vector_t *dirs[] = {
		&theme.apps, &theme.mimes, &theme.places,
		&theme.status, &theme.devices
	};
	const vector_t *hdirs[] = {
		&hicolor->apps, &hicolor->mimes, &hicolor->places,
		&hicolor->status, &hicolor->devices
	};
	icontheme_dir_init(&theme, current->path);
	search_icon_dirs(&theme, current->low_res-1, current->high_res);

	for (unsigned int i = 0; i < 5; i++) {
		if (!(1 << i & option->update_list))
			continue;
		taiwins_cache_dir(cache_file);
		strcat(cache_file, "/");
		strcat(cache_file, name);
		strcat(cache_file, name_from_icon_type(1 << i));

		if (!cache_needs_update(cache_file, current->path) &&
		    !option->force_update)
			continue;
		//retrieve image files
		struct wl_array string_pool, handle_pool;
		wl_array_init(&string_pool);
		wl_array_init(&handle_pool);
		search_icon_imgs(&handle_pool, &string_pool, current->path,
		                 dirs[i]);
		search_icon_imgs(&handle_pool, &string_pool, hicolor_path,
		                 hdirs[i]);
		if (!handle_pool.size)
			goto skip_writing;
		//write cache
		struct image_cache cache =
			image_cache_from_arrays(&handle_pool, &string_pool,
			                        path_to_node);
		int flags = is_file_exist(cache_file) ? O_RDWR : O_RDWR | O_CREAT;
		int fd = open(cache_file, flags, S_IRUSR | S_IWUSR | S_IRGRP);
		image_cache_to_fd(&cache, fd);
#if 0
		cairo_surface_t *surface =
			cairo_image_surface_create_for_data(cache.atlas,
			                                    CAIRO_FORMAT_ARGB32,
			                                    cache.dimension.w, cache.dimension.h,
			                                    cache.dimension.w * 4);
		strcat(cache_file, ".png");
		cairo_surface_write_to_png(surface, cache_file);
		cairo_surface_destroy(surface);
#endif

		close(fd);
		image_cache_release(&cache);
	skip_writing:
		wl_array_release(&handle_pool);
		wl_array_release(&string_pool);
	}
	icontheme_dir_release(&theme);
}

int
main(int argc, char *argv[])
{
	vector_t theme_lookups;
	struct icon_cache_option option;
	struct icon_cache_config *current = NULL;

	if (!parse_arguments(argc, argv, &option))
		return -1;
	if (!create_cache_dir())
		return -1;
	if (!retrieve_themes_to_search(&theme_lookups, &option))
		return -1;

	struct icontheme_dir hicolor_theme;
	icontheme_dir_init(&hicolor_theme, hicolor_path);
	search_icon_dirs(&hicolor_theme, option.low_res-1, option.high_res);

	//now we just do the config
	vector_for_each(current, &theme_lookups)
		update_theme(current, &option, &hicolor_theme);

	icontheme_dir_release(&hicolor_theme);
	vector_destroy(&theme_lookups);
	return 0;
}

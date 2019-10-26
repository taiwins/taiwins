#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include <nk_backends.h>
#include <os/file.h>
#include <sequential.h>
#include <wayland-util.h>


//the struct requires two parts, a image list with name and dimensions, and a
//tile image.

//it is the updater's responsibility to collect all the images and also quick
//hash directories(optional), the readers simply reads the cache and provides
//radix tree to do quick search, another problem here is that is has png/svg/xpm

// then you would have a big ass tile picture that loads everything for you.
// which we have a base resolution,
struct icon_cache_config {
	char *path;
	int res;
	int scale;
	char *hicolor_path; //if not null, search the hicolor path for finding stuff as well.
};

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
	struct dirent *entry;
	DIR *dir = opendir(dir_path);

	if (!dir)
		return count;
	for (entry = readdir(dir); entry; entry = readdir(dir)) {
		char file_path[1024];
		//it could be png or svg
		if (entry->d_type != DT_REG || !strstr(entry->d_name, "png"))
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

int
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
		{&hicolor_resolutions, config->hicolor_path},
		{&resolutions, config->path},
	};

	for (int i = 0; i < 2; i++) {
		struct dirent *entry;
		DIR *dir = NULL;
		int resx, resy;

		vector_init_zero(to_search[i].res, 256, NULL);
		if (!to_search[i].path)
			continue;
		if (!(dir = opendir(to_search[i].path)))
			break;
		for(entry = readdir(dir); entry; entry = readdir(dir)) {
			if (sscanf(entry->d_name, "%dx%d", &resx, &resy) == 2 &&
			    entry->d_type == DT_DIR &&
			    resx <= config->res * config->scale) {
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

int
main(int argc, char *argv[])
{
	struct icon_cache_config config = {
		.path = "/usr/share/icons/Adwaita",
		.hicolor_path = "/usr/share/icons/hicolor",
		.res = 256,
		.scale = 1,
	};
	struct wl_array a, b;
	wl_array_init(&a);
	wl_array_init(&b);
	search_theme(&config, &a, &b);
	off_t *off;
	wl_array_for_each(off, &a) {
		const char *path = (char *)b.data + *off;
		fprintf(stdout, "%s\n", path);
	}
	wl_array_release(&a);
	wl_array_release(&b);
	return 0;
}

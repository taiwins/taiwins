#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include <nk_backends.h>
#include <os/file.h>
#include <sequential.h>


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

//this would in turn be mmap-able
struct icon_cache {
	struct wl_array item_handles; //uint32_t
	struct wl_array item_strings;

	struct wl_array path_handles; //uint32_t
	struct wl_array path_strings;
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

int
search_icon_dir(const char *dir_path,
		struct icon_cache *cache_system)
{
	int count = 0;
	struct dirent *entry;
	DIR *dir = opendir(dir_path);

	if (!dir)
		return count;
	for (entry = readdir(dir); entry; entry = readdir(dir)) {
		char file_path[1024];
		if (entry->d_type != DT_REG || !strstr(entry->d_name, "png"))
			continue;
		strcpy(file_path, dir_path);
		path_concat(file_path, 1024, 1, entry->d_name);
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
	int ret = -1;
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
			//otherwise, we process this dir
		}
	}
out:
	vector_destroy(&resolutions);
	vector_destroy(&hicolor_resolutions);
	return ret;
}

int
main(int argc, char *argv[])
{
	struct icon_cache_config config = {
		.path = "/usr/share/icons/Adwaita",
		.hicolor_path = NULL,
		.res = 512,
		.scale = 1,
	};
	struct wl_array a, b;
	search_theme(&config, &a, &b);

	return 0;
}

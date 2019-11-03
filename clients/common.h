#ifndef TW_CLIENT_COMMON_H
#define TW_CLIENT_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>

#include <os/file.h>

/* helpers for clients */
#ifdef __cplusplus
extern "C" {
#endif


static inline void
taiwins_cache_dir(char cache_home[PATH_MAX])
{
	char *xdg_cache = getenv("XDG_CACHE_HOME");
	if (xdg_cache)
		sprintf(cache_home, "%s/taiwins", xdg_cache);
	else
		sprintf(cache_home, "%s/.cache/taiwins", getenv("HOME"));

}

static inline bool
create_cache_dir(void)
{
	char cache_home[PATH_MAX];
	mode_t cache_mode = S_IRWXU | S_IRGRP | S_IXGRP |
		S_IROTH | S_IXOTH;
	taiwins_cache_dir(cache_home);
	if (mkdir_p(cache_home, cache_mode))
		return false;
	return true;
}


#ifdef __cplusplus
extern "C" {
#endif

#endif /* EOF */

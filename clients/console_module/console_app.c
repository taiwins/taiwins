#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include <rax.h>
#include <vector.h>
#include <helpers.h>
#include "../console.h"


struct xdg_entry {
	char name[256]; //name, all lower case
	char exec[256]; //exec commands
	char icon[256];
	bool terminal_app;
	//have different action support
};

//very quick dirty method
static inline bool
is_section_name(const char *line)
{
	return (*line == '[' && line[strlen(line)-1] == ']');
}

static inline bool
is_empty_line(const char *line)
{
	return strlen(line) == 0;
}


static bool
xdg_entry_from_file(const char *path, struct xdg_entry *entry)
{
	FILE *file = NULL;
	char *line = NULL;
	size_t len = 0;

	if (!is_file_exist(path))
		return false;
	file = fopen(path, "r");
	if (!file)
		return false;

	while ((len = getline(&line, 0, file)) != -1) {
		//get rid of comments first
		char *comment_point = strstr(line, "#");
		char *equal = NULL;
		*comment_point = '\0';

		if (is_empty_line(line))
			goto free_line;
		if (is_section_name(line))
			goto free_line;
		//get key value pair
		equal = strstr(line, "=");
		if (equal)
			*equal = '\0';




	free_line:
		free(line);
		line = NULL;
	}
	if (line)
		free(line);

	fclose(file);
}

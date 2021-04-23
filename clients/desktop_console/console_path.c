/*
 * console_path.h - taiwins client console path module
 *
 * Copyright (c) 2019 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <ctypes/vector.h>
#include <ctypes/helpers.h>
#include <ctypes/os/file.h>
#include "console.h"


static int
console_path_module_exec(struct console_module *module, const char *entry, char **result)
{
	//need a set of application to decide
	if (!is_file_exist(entry))
		return -1;
	//go through all the
	char *ext = strrchr(entry, '.');
	(void)ext;
	ext = 0;
	return 0;
}


static int
console_path_module_search(struct console_module *module, const char *to_search,
			   vector_t *result)
{
	char *cp0 = strdup(to_search);
	char *cp1 = strdup(to_search);
	char *dr = dirname(cp0);
	char *bs = basename(cp1);
	const char *template = "find %s -name \"*%s*\"";

	if (strcmp(dr, ".") == 0)
		dr = getenv("HOME");

	size_t size = strlen(template) + strlen(dr) + strlen(bs);
	char *command = malloc(size);
	sprintf(command, template, dr, bs);

	vector_t buffer;
	FILE *pipe = popen(command, "r");

	vector_init_zero(&buffer, 1, NULL);
	vector_resize(&buffer, 1000);
	while (true) {
		size_t read, to_read = buffer.alloc_len - buffer.len;
		read = fread(vector_at(&buffer, buffer.len),
			     buffer.elemsize, to_read, pipe);
		if (feof(pipe))
			break;
		if (read == to_read)
			vector_resize(&buffer, buffer.len * 2);
	}

	//get lines from buffer.
	FILE *stream = fmemopen(buffer.elems, buffer.len, "r");
	char *line = calloc(buffer.elemsize * buffer.len, 1);
	int len = 0; size_t totalsize = buffer.elemsize * buffer.len;

	//getline has the contains the delimiter
	//do not expect getline allocates memory for you.
	vector_init_zero(result, sizeof(console_search_entry_t),
	                 search_entry_free);
	while ((len = getline(&line, &totalsize, stream)) != -1) {
		//copy to entrys
	}
	free(line);
	fclose(stream);

	free(command);
	free(cp0);
	free(cp1);
	return pclose(pipe);
}


struct console_module path_module = {
	.name = "MODULE_PATH",
	.exec = console_path_module_exec,
	.search = console_path_module_search,
	.support_cache = true,
};

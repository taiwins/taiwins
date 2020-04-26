/*
 * console_app.c - taiwins client console cmd module
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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include <rax.h>
#include <vector.h>
#include <helpers.h>
#include <strops.h>
#include "console_module.h"


/**
 * @brief command search is based on the first word
 */
static bool
console_cmd_module_filter_test(const char *command, const char *candidate)
{
	const char *ptr = command;
	while(*ptr && !isspace(*ptr))
		ptr++;
	return !strncmp(command, candidate, (ptr-command));
}

static int
console_cmd_module_exec(struct console_module *module, const char *entry,
			char **result)
{
	vector_t buffer;
	const char *ptr = entry;
	int len = 0;
	*result = NULL;
	//if the command is not known
	while (*ptr && !isspace(*ptr))
		ptr++;
	if (raxFind(module->radix, (unsigned char *)entry,
		    ptr-entry) == raxNotFound)
		return -1;

	FILE *pipe = popen(entry, "re");//closeonexec
	if (!pipe)
		return -1;

	fcntl(fileno(pipe), F_SETFL, O_NONBLOCK);
	vector_init_zero(&buffer, 1, NULL);
	vector_resize(&buffer, 1000);

	while (true) {
		size_t read, to_read = buffer.len - len;
		read = fread(vector_at(&buffer, len),
			     buffer.elemsize, to_read, pipe);
		len += read;
		//ensure len is always smaller than buffer.len
		if (read == to_read)
			vector_resize(&buffer, buffer.len * 2);
		if (read == 0 || feof(pipe) || ferror(pipe))
			break;
	}
	*(char *)vector_at(&buffer, len) = '\0';
	fprintf(stderr, "%s", (char *)buffer.elems);
	*result = buffer.elems;
	return pclose(pipe);
}

static int
console_cmd_module_search(struct console_module *module, const char *to_search,
			  vector_t *result)
{
	struct raxIterator iter;

	if (!module->radix)
		return 0;
	vector_init_zero(result, sizeof(console_search_entry_t),
			 search_entry_free);

	raxStart(&iter, module->radix);
	//iterator behavior is not yet understood, maybe wrong
	if (raxSeek(&iter, ">=", (unsigned char *)to_search,
		    strlen(to_search)) == 0)
		goto out;
	while (raxNext(&iter)) {
		if (strstr((char *)iter.key, to_search) != (char *)iter.key)
			break;
		console_search_entry_t *entry = vector_newelem(result);
		memset(entry, 0, sizeof(console_search_entry_t));
		if(iter.key_len < 32) {
			strop_ncpy(entry->sstr, (char *)iter.key, iter.key_len+1);
			entry->pstr = NULL;
		} else {
			entry->pstr = calloc(1, iter.key_len+1);
			strop_ncpy(entry->pstr, (char *)iter.key, iter.key_len+1);
		}
	}
out:
	raxStop(&iter);
	return 0;
}

static void
gather_all_cmds(rax *radix)
{
	char *pathes = strdup(getenv("PATH"));
	for (char *sv, *path = strtok_r(pathes, ":", &sv);
	     path; path = strtok_r(NULL, ":", &sv)) {

		DIR *dir = opendir(path);
		if (!dir)
			continue;
		for (struct dirent *entry = readdir(dir); entry;
		     entry = readdir(dir)) {
			if (!strncmp(".", entry->d_name, 255) ||
			    !strncmp("..", entry->d_name, 255))
				continue;
			raxInsert(radix, (unsigned char *)entry->d_name,
				  strlen(entry->d_name), NULL, NULL);
		}
		closedir(dir);
	}
	free(pathes);
}

static void
console_cmd_module_init(struct console_module *module)
{
	module->radix = raxNew();
	gather_all_cmds(module->radix);
}

static void
console_cmd_module_destroy(struct console_module *module)
{
	raxFree(module->radix);
}

struct console_module cmd_module = {
	.name = "MODULE_CMD",
	.filter_test = console_cmd_module_filter_test,
	.exec = console_cmd_module_exec,
	.search = console_cmd_module_search,
	.init_hook = console_cmd_module_init,
	.destroy_hook = console_cmd_module_destroy,
	.support_cache = true,
};

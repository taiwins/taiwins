#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <rax.h>


int main(int argc, char *argv[])
{
	rax *r = raxNew();
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
			raxInsert(r, (unsigned char *)entry->d_name, strlen(entry->d_name), NULL, NULL);
		}
		closedir(dir);
	}

	//so what is this concept of iterating?
	struct raxIterator iter;
	raxStart(&iter, r);
	if (raxSeek(&iter, ">=", (unsigned char *)"a", 1) == 0)
		goto cleanup;
	while(raxNext(&iter)) {
		if (strstr((char *)iter.key, "a") != (char *)iter.key) {
			raxStop(&iter);
			break;
		}

		printf("NEXT: %.*s, val %p\n", (int)iter.key_len,
		       (char*)iter.key,
		       iter.data);
	}

	void *found = raxFind(r, (unsigned char *)"2to3", strlen("2to3"));
	printf("2to3 is%sfound\n", found != raxNotFound ? " " : " not ");
	/* raxShow(r); */

cleanup:
	raxFree(r);
	free(pathes);
	return 0;

}

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
	void *found = raxFind(r, (unsigned char *)"2to3", strlen("2to3"));
	printf("2to3 is%sfound\n", found != raxNotFound ? " " : " not ");
	raxShow(r);
	raxFree(r);
	free(pathes);
	return 0;

}

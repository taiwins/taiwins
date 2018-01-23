#include <stdio.h>
#include <stdlib.h>
#include "hash.h"


/**
 * test the dhash data structure for memory leak
 */

char *randstring(size_t length) { // length should be qualified as const if you follow a rigorous standard

	static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.-#'?!";
	char *randomString;   // initializing to NULL isn't necessary as malloc() returns NULL if it couldn't allocate memory as requested

	if (length) {
		randomString = malloc(length +1); // I removed your `sizeof(char) * (length +1)` as sizeof(char) == 1, cf. C99

		if (randomString) {
			for (int n = 0;n < length;n++) {
				int key = rand() % (int) (sizeof(charset) -1);
				randomString[n] = charset[key];
			}

			randomString[length] = '\0';
		}
	}

	return randomString;
}

void strfree(void *p)
{
	char *s = *(char **)p;
	xfree(s);
}

int main(int argc, char *argv[])
{
	dhashtab_t table;
	dhash_init(&table, hash_djb2, hash_sdbm, hash_cmp_str, sizeof(char *), strfree);
	for (int i = 0; i < 100; i++) {
		char *rand_string = randstring(100);
//		printf("%s\n", rand_string);
		dhash_insert(&table, &rand_string);
	}
//	for (int i = 0; i < table.indices.len; i++) {
//		fprintf(stderr, "%s\n", *(char **)cvector_at(&table.data, *(int *)cvector_at(&table.indices, i)));
//	}
	dhash_destroy(&table);
	return 0;
}

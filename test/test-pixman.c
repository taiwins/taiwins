#include <stdlib.h>
#include <stdio.h>
#include <pixman.h>


int main(int argc, char *argv[])
{
	pixman_region32_t region;
	pixman_region32_init_rect(&region, 0, 0, 100, 100);
	int n = pixman_region32_n_rects(&region);

	pixman_region32_t region1, region2;
	pixman_region32_init_rect(&region1, 0, 0, 50, 50);
	pixman_region32_init(&region2);

	pixman_region32_subtract(&region2, &region, &region1);

	pixman_region32_translate(&region1, 25, 25);
	pixman_region32_t region3;
	pixman_region32_init(&region3);
	pixman_region32_intersect(&region3, &region1, &region2);
	pixman_box32_t *box = pixman_region32_extents(&region3);

	fprintf(stderr, "extents (%d, %d, %d, %d)\n", box->x1, box->y1, box->x2, box->y2);
	fprintf(stderr, "point (%d, %d) is %s the region\n", 35, 35,
		pixman_region32_contains_point(&region3, 35, 35, NULL) ?
		"in" : "not in");

	pixman_region32_fini(&region3);
	pixman_region32_fini(&region2);
	pixman_region32_fini(&region1);
	pixman_region32_fini(&region);


	return 0;
}

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <taiwins/objects/matrix.h>

#define EPSILON 1.0e-6

static bool
matrix_is_identity(const struct tw_mat3 *mat)
{
	bool ret = true;
	for (int i = 0; i < 9; i++) {
		ret = (i == 0 || (i % 4 == 0)) ?
			fabs(mat->d[i] - 1.0) < EPSILON :
			fabs(mat->d[i] - 0.0) < EPSILON;
		if (!ret)
			return false;
	}
	return ret;
}

static void
setup_random(void)
{
	struct timespec timespec;
	clock_gettime(CLOCK_MONOTONIC, &timespec);
	srand(timespec.tv_sec);
}

static bool
identity_test(void)
{
	struct tw_mat3 mat;

	tw_mat3_init(&mat);
	return matrix_is_identity(&mat);
}

static bool
rotation_test()
{
	int rand_degree = rand() % 360;
	struct tw_mat3 rot;
	struct tw_mat3 inv_rot;
	struct tw_mat3 mul;

	tw_mat3_rotate(&rot, rand_degree, true);
	tw_mat3_rotate(&inv_rot, -rand_degree, true);
	tw_mat3_multiply(&mul, &rot, &inv_rot);
	return matrix_is_identity(&mul);
}

static bool
translation_test()
{
	int rand_x = rand();
	int rand_y = rand();
	struct tw_mat3 trans;
	struct tw_mat3 inv_trans;
	struct tw_mat3 mul;

	tw_mat3_translate(&trans, rand_x, rand_y);
	tw_mat3_translate(&inv_trans, -rand_x, -rand_y);
	tw_mat3_multiply(&mul, &trans, &inv_trans);
	return matrix_is_identity(&mul);
}

static bool
scale_test()
{
	float rand_x = rand();
	float rand_y = rand();
	struct tw_mat3 scal;
	struct tw_mat3 inv_scal;
	struct tw_mat3 mul;

	tw_mat3_scale(&scal, rand_x, rand_y);
	tw_mat3_scale(&inv_scal, 1.0/rand_x, 1.0/rand_y);
	tw_mat3_multiply(&mul, &scal, &inv_scal);
	return matrix_is_identity(&mul);
}

static bool
compound_test()
{
	int trans_x = rand();
	int trans_y = rand();
	int degree = rand() % 360;
	float scal_x = rand();
	float scal_y = rand();

	struct tw_mat3 trans, inv_trans;
	struct tw_mat3 scal, inv_scal;
	struct tw_mat3 rot, inv_rot;
	struct tw_mat3 mul;

	tw_mat3_rotate(&rot, degree, false);
	tw_mat3_rotate(&inv_rot, -degree, false);
	tw_mat3_translate(&trans, trans_x, trans_y);
	tw_mat3_translate(&inv_trans, -trans_x, -trans_y);
	tw_mat3_scale(&scal, scal_x, scal_y);
	tw_mat3_scale(&inv_scal, 1.0/scal_x, 1.0/scal_y);

	tw_mat3_multiply(&mul, &rot, &scal);
	tw_mat3_multiply(&mul, &trans, &mul);
	tw_mat3_multiply(&mul, &inv_trans, &mul);
	tw_mat3_multiply(&mul, &inv_rot, &mul);
	tw_mat3_multiply(&mul, &inv_scal, &mul);
	return matrix_is_identity(&mul);
}

static bool
inverse_test_simple()
{
	struct tw_mat3 M, inv, mul;

	M.d[0] = 1.0; M.d[1] = 2.0; M.d[2] = 3.0;
	M.d[3] = 0.0; M.d[4] = 1.0; M.d[5] = 4.0;
	M.d[6] = 5.0; M.d[7] = 6.0; M.d[8] = 0.0f;

	tw_mat3_inverse(&inv, &M);
	tw_mat3_multiply(&mul, &M, &inv);
	return matrix_is_identity(&mul);
}

static bool
transform_inverse_test()
{
	int trans_x = rand();
	int trans_y = rand();
	int degree = rand() % 360;
	float scal_x = rand();
	float scal_y = rand();

	struct tw_mat3 trans, scal, rot;
	struct tw_mat3 mul, inv;

	tw_mat3_rotate(&rot, degree, true);
	tw_mat3_translate(&trans, trans_x, trans_y);
	tw_mat3_scale(&scal, scal_x, scal_y);

	tw_mat3_multiply(&mul, &scal, &rot);
	tw_mat3_multiply(&mul, &trans, &mul);
	tw_mat3_inverse(&inv, &mul);
	tw_mat3_multiply(&mul, &inv, &mul);
	return matrix_is_identity(&mul);
}

int main(int argc, char *argv[])
{
	setup_random();
	if (!identity_test())
		goto err;
	for (int i = 0; i < 100; i++)
		if (!rotation_test())
			goto err;
	for (int i = 0; i < 100; i++)
		if (!translation_test())
			goto err;
	for (int i = 0; i < 100; i++)
		if (!scale_test())
			goto err;
	for (int i = 0; i < 10; i++)
		if (!compound_test())
			goto err;
	if (!inverse_test_simple())
		goto err;
	for (int i = 0; i < 10; i++)
		if (!transform_inverse_test())
			goto err;
	//should work without inverse
	return 0;
err:
	perror("matrix test failed!");
}

#include <sequential.h>



int main(int argc, char *argv[])
{
	queue_t queue;
	queue_init(&queue, sizeof(int), NULL);
	for (int i = 0; i < 100; i++) {
		queue_append(&queue, &i);
	}
	for (int i = 0; i < 100; i++) {
		fprintf(stderr, "%d\t", *(int *)vector_at(&queue.vec, i));
	}
	for (int i = 0; i < 50; i++)
		queue_pop(&queue);
	queue_destroy(&queue);
	return 0;
}

#include <libudev.h>
#include <stdio.h>
#include "../clients/client.h"
#include <sys/inotify.h>

int recieve_callback(void *p, int fd)
{
	fprintf(stderr, "I recieved a event\n");
	return TW_EVENT_NOOP;
}


struct tw_event event = {
	.data = NULL,
	.cb = recieve_callback,
};


int main(int argc, char *argv[])
{
	const char *file = argv[1];

	struct tw_event_queue queue = {0};
	tw_event_queue_init(&queue);
	/* int fd = open(file, O_RDONLY); */
	tw_event_queue_add_file(&queue, argv[1], &event, 0);
	tw_event_queue_run(&queue);

	return 0;
}

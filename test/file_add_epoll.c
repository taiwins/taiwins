#include <unistd.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <wayland-client.h>





//we have to use epoll.
int main(int argc, char *argv[])
{
	uint64_t nhit;
	struct epoll_event event = {0};
	event.events = EPOLLET | EPOLLIN;
	event.data.ptr = NULL;

	struct epoll_event allevents[32];
	int efd = epoll_create1(EPOLL_CLOEXEC);
	if (efd < 0)
		return -1;

	struct itimerspec its = {0};
	int tfds[10];
	for (int i = 0; i < 10; i++) {
		tfds[i] = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
		its.it_value.tv_sec = (i+1);
		its.it_value.tv_nsec = 100 * (i+1);
		its.it_interval.tv_sec = (i+1);
		its.it_interval.tv_nsec = 100 * (i+1);
		if (timerfd_settime(tfds[i], 0, &its, NULL))
			return -1;
//		read(tfds[i], &nhit, 8);

		event.events = EPOLLET | EPOLLIN;
		event.data.fd = tfds[i];
		if (epoll_ctl(efd, EPOLL_CTL_ADD, tfds[i], &event)) {
			printf("error in adding timer.\n");
			return -1;
		}
	}
	fprintf(stderr, "timer set!\n");
	while(true) {
		long nhit;
		int count = epoll_wait(efd, allevents, 32, -1);
		for (int i = 0; i < count; i++) {
			read(allevents[i].data.fd, &nhit, 8);
		}
		fprintf(stderr, "%d timer hit\n", count);
	}
	return 0;
}

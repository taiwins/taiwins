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
	struct epoll_event event = {0};
	event.events = EPOLLET | EPOLLIN;
	event.data.ptr = NULL;

	struct epoll_event allevents[32];

	struct itimerspec its = {0};
	its.it_interval.tv_sec = 1;
	its.it_interval.tv_nsec = 1000;
	int efd = epoll_create1(EPOLL_CLOEXEC);
	int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	event.data.fd  = tfd;
	if (timerfd_settime(tfd, 0, &its, NULL)) {
		fprintf(stderr, "settime failed.\n");
		return -1;
	}
	uint64_t timelapsed;
	epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &event);
	for (int j = 0; j < 100; j++)  {
		int count = epoll_wait(efd, allevents, 32, 0);
		printf("%d", count);
		for (int i = 0; i < count; i++) {
			read(tfd, &timelapsed, 8);
		}
	}
	return 0;
}

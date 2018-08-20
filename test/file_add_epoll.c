#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <wayland-client.h>


//we have to use epoll.
int main(int argc, char *argv[])
{
	struct epoll_event event = {0};
	event.events |= EPOLLIN;
	event.events |= EPOLLOUT;
	event.data.ptr = NULL;
	struct epoll_event allevents[32];

	struct itimerspec its;
	its.it_interval.tv_sec = 1;
	its.it_interval.tv_nsec = 100;
	int efd = epoll_create1(EPOLL_CLOEXEC);
	int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_NONBLOCK);
	timerfd_settime(tfd, 0, &its, NULL);

	epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &event);
	int count = epoll_wait(efd, allevents, 32, 10000);
	for (int i = 0; i < count; i++) {

	}
	return 0;
}

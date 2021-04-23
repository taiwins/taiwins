/*
 * subprocess.c - taiwins subprocess handler
 *
 * Copyright (c) 2020 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <wayland-server-core.h>
#include <ctypes/os/os-compatibility.h>

#include <taiwins/objects/logger.h>
#include <taiwins/objects/subprocess.h>


static int
tw_launch_default_fork(pid_t pid, struct tw_subprocess *chld)
{
	return 0;
}

static int
tw_launch_default_exec(const char *path, struct tw_subprocess *chld)
{
	if (execlp(path, path, NULL) == -1) {
		tw_logl("tw_launch_client: "
		        "failed to exec the client %s\n", path);
		return -1;
	}
	return 0;
}

WL_EXPORT struct wl_client *
tw_launch_client_complex(struct wl_display *display, const char *path,
                         struct tw_subprocess *chld,
                         int (*fork_cb)(pid_t, struct tw_subprocess *),
                         int (*exec_cb)(const char *,struct tw_subprocess *))
{
	int sv[2], fd;
	pid_t pid;
	struct wl_client *client = NULL;
	struct wl_list *clients = tw_get_clients_head();
	char socket_fd_str[12];
	sigset_t allsignals;

	if (!fork_cb)
		fork_cb = tw_launch_default_fork;
	if (!exec_cb)
		exec_cb = tw_launch_default_exec;

	//always need to create wayland socket
	if (os_socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv)) {
		tw_logl("taiwins client launch: "
		        "failed to create the socket for client %s\n",
		        path);
		return NULL;
	}

	pid = fork();
	if (pid == -1) {
		close(sv[0]);
		close(sv[1]);
		tw_logl("taiwins client launch: "
		        "failed to create new process, %s\n",
		        path);
		return NULL;
	} else if (pid == 0) {
		//child holds sv[1] and closes sv[0]
		close(sv[0]);

		if (seteuid(getuid()) == -1)
			goto fail;

		//unblocking signals
		sigfillset(&allsignals);
		sigprocmask(SIG_UNBLOCK, &allsignals, NULL);
		//duplicate the socket since it is close-on-exec
		fd = dup(sv[1]);
		snprintf(socket_fd_str, sizeof(socket_fd_str), "%d", fd);
		setenv("WAYLAND_SOCKET", socket_fd_str, 1);
		//do the fork
		if (fork_cb(pid, chld) ||  exec_cb(path, chld))
			goto fail;
	fail:
		close(sv[1]);
		_exit(-1);
	} else {
		//parent holds sv[0] and closes sv[1]
		close(sv[1]);
		if (fork_cb(pid, chld))
			goto fail_p;
		client = wl_client_create(display, sv[0]);
		if (!client) {
			tw_logl("taiwins_client_launch: "
			        "failed to create wl_client for %s\n", path);
			return NULL;
		}
		if (chld) {
			chld->pid = pid;
			wl_list_init(&chld->link);
			wl_list_insert(clients, &chld->link);
		}
		return client;
	fail_p:
		close(sv[0]);
		return NULL;
	}
	return client;

}

WL_EXPORT struct wl_client *
tw_launch_client(struct wl_display *display, const char *path,
                 struct tw_subprocess *chld)
{
	return tw_launch_client_complex(display, path, chld, NULL, NULL);
}

WL_EXPORT struct wl_list *
tw_get_clients_head()
{
	static struct wl_list clients_head = {
		.prev = &clients_head,
		.next = &clients_head,
	};

	return &clients_head;
}

WL_EXPORT void
tw_end_client(struct wl_client *client)
{
	pid_t pid; uid_t uid; gid_t gid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	kill(pid, SIGINT);
}

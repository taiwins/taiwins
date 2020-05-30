/*
 * taiwins.c - taiwins server shared functions
 *
 * Copyright (c) 2019-2020 Xichen Zhou
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dlfcn.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <libweston/libweston.h>
#include <wayland-util.h>
#include <ctypes/os/os-compatibility.h>

#include "taiwins.h"

FILE *tw_logfile = NULL;

int
tw_log(const char *format, va_list args)
{
	if (tw_logfile)
		return vfprintf(tw_logfile, format, args);
	return -1;
}

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

struct wl_client *
tw_launch_client_complex(struct weston_compositor *ec, const char *path,
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
		client = wl_client_create(ec->wl_display, sv[0]);
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

struct wl_client *
tw_launch_client(struct weston_compositor *ec, const char *path,
                 struct tw_subprocess *chld)
{
	return tw_launch_client_complex(ec, path, chld, NULL, NULL);
}

struct wl_list *tw_get_clients_head()
{
	static struct wl_list clients_head = {
		.prev = &clients_head,
		.next = &clients_head,
	};

	return &clients_head;
}

void
tw_end_client(struct wl_client *client)
{
	pid_t pid; uid_t uid; gid_t gid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
	kill(pid, SIGINT);
}

bool
tw_set_socket(struct wl_display *display)
{
	char path[PATH_MAX];
	unsigned int socket_num = 0;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	//get socket
	while(true) {
		sprintf(path, "%s/wayland-%d", runtime_dir, socket_num);
		if (access(path, F_OK) != 0) {
			sprintf(path, "wayland-%d", socket_num);
			break;
		}
		socket_num++;
	}
	if (wl_display_add_socket(display, path)) {
		tw_logl("EE:failed to add socket %s", path);
		return false;
	}
	return true;
}

int
tw_term_on_signal(int sig_num, void *data)
{
	struct wl_display *display = data;

	tw_logl("Caught signal %d\n", sig_num);
	wl_display_terminate(display);
	return 1;
}

int
tw_handle_sigchld(UNUSED_ARG(int sig_num), UNUSED_ARG(void *data))
{
	struct wl_list *head;
	struct tw_subprocess *subproc;
	int status;
	pid_t pid;

	head = tw_get_clients_head();

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		wl_list_for_each(subproc, head, link)
			if (pid == subproc->pid)
				break;

		if (&subproc->link == head) {
			weston_log("unknown process exited\n");
			continue;
		}

		wl_list_remove(&subproc->link);
		if (subproc->chld_handler)
			subproc->chld_handler(subproc, status);
	}
	if (pid < 0 && errno != ECHILD)
		weston_log("error in waiting child with status %s\n",
		           strerror(errno));
	return 1;
}

void
tw_lose_surface_focus(struct weston_surface *surface)
{
	struct weston_compositor *ec = surface->compositor;
	struct weston_seat *seat;
	struct weston_keyboard *keyboard;

	wl_list_for_each(seat, &ec->seat_list, link) {
		 keyboard = weston_seat_get_keyboard(seat);
		if (keyboard &&
		    (weston_surface_get_main_surface(keyboard->focus) == surface))
			weston_keyboard_set_focus(keyboard, NULL);
		//it maynot be a good idea to do the pointer and touch as well,
		//since FIRST only keyboard gets the focus of a surface, the
		//rest gets the focus from view; SECOND if we do this when we
		//need focused output, there is no thing we can do
	}
}

void
tw_focus_surface(struct weston_surface *surface)
{
	struct weston_seat *active_seat;
	struct weston_keyboard *keyboard;

	wl_list_for_each(active_seat, &surface->compositor->seat_list, link) {
		keyboard = active_seat->keyboard_state;
		if (keyboard) {
			weston_keyboard_set_focus(keyboard, surface);
			break;
		}
	}
}

struct weston_output *
tw_get_focused_output(struct weston_compositor *compositor)
{
	struct weston_seat *seat;
	struct weston_output *output = NULL;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_touch *touch = weston_seat_get_touch(seat);
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		/* Priority has touch focus, then pointer and
		 * then keyboard focus. We should probably have
		 * three for loops and check frist for touch,
		 * then for pointer, etc. but unless somebody has some
		 * objections, I think this is sufficient. */
		if (touch && touch->focus)
			output = touch->focus->output;
		else if (pointer && pointer->focus)
			output = pointer->focus->output;
		else if (keyboard && keyboard->focus)
			output = keyboard->focus->output;

		if (output)
			break;
	}

	return output;
}

void *
tw_load_weston_module(const char *name, const char *entrypoint)
{
	void *module, *init;

	if (name == NULL || entrypoint == NULL)
		return NULL;

	//our modules are in the rpath as we do not have the
	//LIBWESTON_MODULEDIR, so we need to test name and
	module = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
	if (module) {
		weston_log("Module '%s' already loaded\n", name);
		return NULL;
	} else {
		module = dlopen(name, RTLD_NOW);
		if (!module) {
			weston_log("Failed to load the module %s\n", name);
			return NULL;
		}
	}

	init = dlsym(module, entrypoint);
	if (!init) {
		weston_log("Faild to lookup function in module: %s\n",
		           dlerror());
		dlclose(module);
		return NULL;
	}
	return init;

}

void
tw_layer_set_position(struct tw_layer *layer, enum tw_layer_pos pos,
                      struct wl_list *layers)
{
	struct tw_layer *l, *tmp;

	wl_list_remove(&layer->link);
	layer->position = pos;

	//from bottom to top
	wl_list_for_each_reverse_safe(l, tmp, layers, link) {
		if (l->position >= pos) {
			wl_list_insert(&l->link, &layer->link);
			return;
		}
	}
	wl_list_insert(layers, &layer->link);
}

void
tw_layer_unset_position(struct tw_layer *layer)
{
	wl_list_remove(&layer->link);
	wl_list_init(&layer->link);
}

/*
 * direct.c - taiwins login direct implementation
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

#include "options.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <wayland-util.h>
#include <xf86drm.h>
#if defined (__linux__)
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/input.h>
#include <linux/major.h>
#elif defined(__FreeBSD__)
/* we only include the header here, the implementation is pending */
#include <sys/consio.h>
#include <sys/kbio.h>
#endif

#include <wayland-client-core.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/backend_drm.h>

#include "login.h"

#define DRM_MAJOR 226
#define MAX_GPUS 16

struct tw_login_fd {
	struct wl_list link;
	int fd;
};

struct tw_direct_login {
	struct tw_login base;
	struct wl_display *display;
	struct wl_event_source *vt_source;
	int tty_fd;
	int socket_fd;
	int vtnr;
	int orig_kbmode;

	struct tw_login_fd gpus[MAX_GPUS];
	struct wl_list used_fds;
	struct wl_list avail_fds;
};

static inline void
set_masters(struct tw_direct_login *direct)
{
	struct tw_login_fd *gpu;
	wl_list_for_each(gpu, &direct->used_fds, link)
		drmSetMaster(gpu->fd);
}

static inline void
drop_masters(struct tw_direct_login *direct)
{
	struct tw_login_fd *gpu;
	wl_list_for_each(gpu, &direct->used_fds, link)
		drmDropMaster(gpu->fd);
}

static inline void
close_gpus(struct tw_direct_login *direct)
{
	struct tw_login_fd *gpu, *tmp;
	wl_list_for_each_safe(gpu, tmp, &direct->used_fds, link) {
		close(gpu->fd);
		wl_list_remove(&gpu->link);
		wl_list_insert(direct->avail_fds.prev, &gpu->link);
	}

}

static inline bool
check_master(int fd)
{
	drm_magic_t magic;

	return drmGetMagic(fd, &magic) == 0 &&
		drmAuthMagic(fd, magic) == 0;
}

#if defined ( __linux__ )

static int
handle_vt_change(int signo, void *data)
{
	struct tw_direct_login *direct = data;
	bool active = !direct->base.active;

	if (direct->base.active) {
		ioctl(direct->tty_fd, VT_RELDISP, VT_ACKACQ);
		set_masters(direct);
	} else {
		//restore the session
		drop_masters(direct);
		ioctl(direct->tty_fd, VT_RELDISP, 1);
	}

	tw_login_set_active(&direct->base, active);
	return 1;
}

static bool
setup_tty(struct tw_direct_login *direct, struct wl_display *display)
{
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	struct vt_mode vt_mode = {
		.mode  = VT_PROCESS,
		.relsig = SIGUSR2,
		.acqsig = SIGUSR2,
	};
	struct vt_stat vt_stat;
	const char *tty_path = "/dev/tty";
	int tty, kd_mode, orig_kbmode;

	int fd = open(tty_path, O_RDWR | O_CLOEXEC);
	if (fd == -1) {
		tw_logl_level(TW_LOG_ERRO, "Cannot open %s", tty_path);
		return false;
	}

	if (ioctl(fd, VT_GETSTATE, &vt_stat)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query current vtnr");
		goto err;
	}
	tty = vt_stat.v_active;

	if (ioctl(fd, KDGETMODE, &kd_mode)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to get tty mode");
		goto err;
	}
	if (kd_mode != KD_TEXT) {
		tw_logl_level(TW_LOG_ERRO, "TTY is not in text mode");
		goto err;
	}
	if (ioctl(fd, KDGKBMODE, &orig_kbmode)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to get keyboard mode");
		goto err;
	}
	if (ioctl(fd, KDSKBMODE, K_OFF)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to set keyboard mode");
		goto err;
	}
	if (ioctl(fd, KDSETMODE, KD_GRAPHICS)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to set graphics mode");
		goto err;
	}
	if (ioctl(fd, VT_SETMODE, &vt_mode) < 0) {
		tw_logl_level(TW_LOG_ERRO, "Failed to take control tty");
		goto err;
	}

	direct->tty_fd = fd;
	direct->vtnr = tty;
	direct->orig_kbmode = orig_kbmode;
	//TODO: add signals for SIGUSR2
	direct->vt_source = wl_event_loop_add_signal(loop, SIGUSR2,
	                                             handle_vt_change, direct);
	if (!direct->vt_source)
		goto err;
	return true;

err:
	close(fd);
	return false;
}

static void
close_tty(struct tw_direct_login *direct)
{
	struct vt_mode mode = {
		.mode = VT_AUTO,
	};
	errno = 0;
	ioctl(direct->tty_fd, KDSKBMODE, direct->orig_kbmode);
	ioctl(direct->tty_fd, KDSETMODE, KD_TEXT);
	drop_masters(direct);
	ioctl(direct->tty_fd, VT_SETMODE, &mode);
	if (errno)
		tw_logl_level(TW_LOG_ERRO, "Failed to restore tty");
	wl_event_source_remove(direct->vt_source);
	close(direct->tty_fd);
}

static int
handle_direct_open(struct tw_login *base, const char *path)
{
	struct tw_direct_login *direct = wl_container_of(base, direct, base);
	struct tw_login_fd *gpu = NULL;
	//TODO adding flags!
	struct stat st;

	if (wl_list_empty(&direct->avail_fds))
		return -1;
	gpu = wl_container_of(direct->avail_fds.next, gpu, link);
	wl_list_remove(&gpu->link);

	if ((gpu->fd = open(path, O_CLOEXEC)) == -1)
		goto err_open;
	if (fstat(gpu->fd, &st) == -1)
		goto err_stat;

	if (major(st.st_rdev) != DRM_MAJOR)
		goto err_stat;
	if (!check_master(gpu->fd))
		goto err_stat;
	wl_list_insert(direct->used_fds.prev, &gpu->link);

	return gpu->fd;
err_stat:
	close(gpu->fd);
err_open:
	wl_list_insert(direct->avail_fds.prev, &gpu->link);
	return -1;
}

static void
handle_direct_close(struct tw_login *base, int fd)
{
	struct tw_login_fd *gpu, *tmp;
	struct tw_direct_login *direct = wl_container_of(base, direct, base);

	wl_list_for_each_safe(gpu, tmp, &direct->used_fds, link) {
		if (gpu->fd == fd) {
			close(fd);
			wl_list_remove(&gpu->link);
			wl_list_insert(direct->avail_fds.prev, &gpu->link);
			return;
		}
	}
	close(fd);
}

static int
handle_direct_get_vt(struct tw_login *login)
{
	struct tw_direct_login *direct = wl_container_of(login, direct, base);
	struct vt_stat vt_stat;

	if (ioctl(direct->tty_fd, VT_GETSTATE, &vt_stat) != 0)
		return -1;

	return vt_stat.v_active;
}

static bool
handle_direct_switch_vt(struct tw_login *login, unsigned int vt)
{
	struct tw_direct_login *direct = wl_container_of(login, direct, base);

	return ioctl(direct->tty_fd, VT_ACTIVATE, vt) == 0;
}

#elif defined(__FreeBSD__)

static int
handle_direct_get_vt(struct tw_login *login)
{
	struct tw_direct_login *direct = wl_container_of(login, direct, base);
	int tty0_fd, old_tty = 0;

	if ((tty0_fd = open("/dev/ttyv0", O_RDWR | O_CLOEXEC)) < 0)
		return -1;
	if (ioctl(direct->tty_fd, VT_GETACTIVE, &old_tty) != 0) {
		close(tty0_fd);
		return -1;
	}
	close(tty0_fd);
	return old_tty;;
}

//TODO

#endif

static const struct tw_login_impl direct_impl = {
	.open = handle_direct_open,
	.close = handle_direct_close,
	.get_vt = handle_direct_get_vt,
	.switch_vt = handle_direct_switch_vt,
};

struct tw_login *
tw_login_create_direct(struct wl_display *display)
{
	struct tw_direct_login *direct = calloc(1, sizeof(*direct));
	const char *seat = getenv("XDG_SEAT");

	if (!direct)
		return NULL;
	if (!tw_login_init(&direct->base, display, &direct_impl))
		goto err_init;
	direct->display = display;

	if (strcmp(seat, "seat0") == 0) {
		if (!setup_tty(direct, display))
			goto err_ipc;
	}
	//create enough GPUS to open
	wl_list_init(&direct->avail_fds);
	wl_list_init(&direct->used_fds);
	for (int i = 0; i < MAX_GPUS; i++) {
		direct->gpus[i].fd = -1;
		wl_list_init(&direct->gpus[i].link);
		wl_list_insert(direct->avail_fds.prev, &direct->gpus[i].link);
	}

	return &direct->base;
err_ipc:
	tw_login_fini(&direct->base);
err_init:
	free(direct);
	return NULL;
}

void
tw_login_destroy_direct(struct tw_login *login)
{
	struct tw_direct_login *direct = wl_container_of(login, direct, base);

	close_tty(direct);
	close_gpus(direct);
	close(direct->socket_fd);
	tw_login_fini(login);
	free(direct);
}

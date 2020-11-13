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
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
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

struct tw_direct_login {
	struct tw_login base;
	struct wl_display *display;
	struct wl_event_source *vt_source;
	int tty_fd;
	int socket_fd;
	int vtnr;
	int orig_kbmode;
};

static const struct tw_login_impl direct_impl = {

};

static int
handle_vt_change(int signo, void *data)
{
	struct tw_direct_login *direct = data;
	bool active = !direct->base.active;

	if (direct->base.active) {
		//drop the session;
		//TODO setmaster
		ioctl(direct->tty_fd, VT_RELDISP, 1);
	} else {
		//restore the session
		//TODO dropmaster
		ioctl(direct->tty_fd, VT_RELDISP, VT_ACKACQ);
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
	if (ioctl(fd, KDGETMODE, &orig_kbmode)) {
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

struct tw_login *
tw_login_create_direct(struct wl_display *display)
{
	struct tw_direct_login *direct = calloc(1, sizeof(*direct));
	const char *seat = getenv("XDG_SEAT");

	if (!direct)
		return NULL;
	if (!tw_login_init(&direct->base, display, &direct_impl)) {
		free(direct);
		return NULL;
	}
	direct->display = display;

	if (strcmp(seat, "seat0") == 0) {
		if (!setup_tty(direct, display))
			goto err_ipc;
	}

	return &direct->base;
}

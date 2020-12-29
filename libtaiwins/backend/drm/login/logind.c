/*
 * logind.c - taiwins login logind implementation
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/backend_drm.h>
#include <taiwins/objects/logger.h>
#include <taiwins/dbus_utils.h>
#include <tdbus.h>

#include "login.h"
#include "tdbus_message.h"
#include "tdbus_message_iter.h"

#if _TW_HAS_SYSTEMD
	#include <systemd/sd-bus.h>
	#include <systemd/sd-login.h>
#elif _TW_HAS_ELOGIND
	#include <elogind/sd-bus.h>
	#include <elogind/sd-login.h>
#endif

#define LOGIND_DEST "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_SEAT_IFACE "org.freedesktop.login1.Seat"
#define LOGIND_MANAGER_IFACE "org.freedesktop.login1.Manager"
#define LOGIND_SESSION_IFACE "org.freedesktop.login1.Session"
#define LOGIND_PROPERTY_IFACE "org.freedesktop.DBus.Properties"

static const struct tw_login_impl logind_impl;

struct tw_logind_login {
	struct tw_login base;
	char *session_id, *session_org_path, *seat_path;
	unsigned vtnr;

	struct wl_display *display;
	struct tdbus *bus;
	struct wl_event_source *event;
};

static inline struct tw_logind_login *
tw_logind_login_from_base(struct tw_login *base)
{
	struct tw_logind_login *logind;
	assert(base->impl == &logind_impl);
	logind = wl_container_of(base, logind, base);
	return logind;
}

/******************************************************************************
 * dbus functions
 *
 * logind uses d-bus for providing session functions
 *
 * The message handler requires takes dicts and variants, which tdbus does not
 * handle at the moment. Also, tdbus is not blocking(this can be easily
 * changed). There are some example singals
 * Session Properties changing: PropertyChanged(s,a{sv},as);
 * Seat Properties Changing: PropertyChanged(s,a{sv},as);
 *
 * signals we want to watch
 * LOGIN_MANAGERS: SessionRemoved
 * SESSION: (PauseDevice, ResumeDevice, PropertyChanged)
 * seat: (PropertyChanged)
 *
 *****************************************************************************/

static bool
logind_bus_setup_dbus_path(struct tw_logind_login *logind)
{
	char *path_session = NULL, *path_seat = NULL;
	struct tdbus_reply session_reply, seat_reply;
	struct tdbus *bus = logind->bus;

	if (!tdbus_send_method_call(bus, LOGIND_DEST, LOGIND_PATH,
	                            LOGIND_MANAGER_IFACE, "GetSession",
	                            &session_reply, "s", logind->session_id)) {
		tw_logl_level(TW_LOG_WARN, "Failed to get session path");
		goto out;
	}
	if (!tdbus_send_method_call(bus, LOGIND_DEST, LOGIND_PATH,
	                            LOGIND_MANAGER_IFACE, "GetSeat",
	                            &seat_reply, "s", logind->base.seat)) {
		tw_logl_level(TW_LOG_WARN, "Failed to get seat path");
		goto out;
	}

	if (!tdbus_read(session_reply.message, "o", &path_session))
		goto out;
	if (!tdbus_read(seat_reply.message, "o", &path_seat))
		goto out;
	logind->session_org_path = path_session;
	logind->seat_path = path_seat;

	tdbus_free_message(session_reply.message);
	tdbus_free_message(seat_reply.message);
	return true;
out:
	if (session_reply.message)
		tdbus_free_message(session_reply.message);
	if (seat_reply.message)
		tdbus_free_message(seat_reply.message);
	if (path_session)
		free(path_session);
	if (path_seat)
		free(path_seat);

	return false;
}

static void
logind_bus_get_active(struct tw_logind_login *logind)
{
	struct tdbus_reply reply = {0};
	const char *iface = LOGIND_SESSION_IFACE;
	const char *name = "Active";
	struct tdbus_message_arg vread = {0};
	bool active;

	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_PROPERTY_IFACE, "Get", &reply,
	                            "ss", iface, name))
		return;
	if (!tdbus_read(reply.message, "v", &vread))
		goto out;
	if (strcmp(vread.arg.variant.signature, "b") == 0 &&
	    vread.arg.variant.arg) {
		active = vread.arg.variant.arg->arg.b;
		tw_login_set_active(&logind->base, active);
	}
out:
	if (vread.arg.variant.arg)
		tdbus_msg_done_variant(&vread);
	if (reply.message)
		tdbus_free_message(reply.message);

}

static int
logind_bus_handle_session_removed(const struct tdbus_signal *signal)
{
	struct tw_logind_login *logind = signal->user_data;

	tw_logl("logind Session removed !");
	wl_display_terminate(logind->display);
	return 0;
}

static int
logind_bus_handle_pause_device(const struct tdbus_signal *signal)
{
	struct tw_logind_login *logind = signal->user_data;

	uint32_t major, minor;
	char *type = NULL;
	if (!tdbus_read(signal->message, "uus", &major, &minor, &type)) {
		tw_log_level(TW_LOG_WARN, "Failed to parse pause device msg");
		goto out;
	}
	if (strcmp(type, "gone"))
		tw_login_set_active(&logind->base, false);
	if (strcmp(type, "paused") == 0)
		if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
		                            logind->session_org_path,
		                            LOGIND_SESSION_IFACE,
		                            "PauseDeviceComplete", NULL,
		                            "uu", major, minor))
			tw_logl_level(TW_LOG_WARN, "Failed to pause device");
out:
	if (type)
		free(type);
	return 0;
}

static int
logind_bus_handle_resume_device(const struct tdbus_signal *signal)
{
	int fd;
	uint32_t major, minor;
	struct tw_logind_login *logind = signal->user_data;

	if (!tdbus_read(signal->message, "uuh", &major, &minor, &fd)) {
		tw_log_level(TW_LOG_WARN, "Failed to parse resume device msg");
		return 0;
	}
	tw_login_set_active(&logind->base, true);

	//TODO handle the new fd provided by device.

	return 0;
}

static int
logind_bus_handle_session_properties_changed(const struct tdbus_signal *signal)
{
	char **strings = NULL, *interface = NULL;
	int nstr = 0, ne = 0;
	struct tdbus_arg_dict_entry *entries = NULL;
	struct tw_logind_login *logind = signal->user_data;

	if (!tdbus_read(signal->message, "sa{sv}as",
	                &interface, &nstr, &strings, &ne, &entries)) {
		tw_logl_level(TW_LOG_WARN, "Failed to parse property change");
		goto out;
	}
	//ignored
	if (strcmp(interface, LOGIND_SESSION_IFACE) != 0)
		goto out;
	for (struct tdbus_arg_dict_entry *e = entries; e < entries + ne; e++) {
		if (e->key.type != TDBUS_ARG_STRING ||
		    e->val.type != TDBUS_ARG_VARIANT)
			continue;
		if (strcmp(e->key.arg.str, "Active") == 0) {
			bool active;
			struct tdbus_arg_variant *v = &e->val.arg.variant;
			struct tdbus_message_arg *arg = v->arg;
			if ((strcmp(v->signature, "b") == 0)) {
				active = arg->arg.b;
				tw_login_set_active(&logind->base, active);
			}
		}
	}
	for (int i = 0; i < nstr; i++) {
		if (strcmp(strings[i], "Active") == 0)
			logind_bus_get_active(logind);
	}

out:
	if (interface)
		free(interface);
	if (strings)
		tdbus_msg_free_array(strings, nstr, TDBUS_ARG_STRING);
	if (entries)
		tdbus_msg_free_array(entries, ne, TDBUS_ARG_DICT_ENTRY);
	return 0;
}

static int
logind_bus_handle_seat_properties_changed(const struct tdbus_signal *signal)
{
	//TODO mostly dealing with can graphical property
	return 0;
}

static bool
logind_bus_add_signals(struct tw_logind_login *logind)
{
	if (!tdbus_match_signal(logind->bus, LOGIND_DEST, LOGIND_MANAGER_IFACE,
	                        "SessionRemoved", LOGIND_PATH, logind,
	                        logind_bus_handle_session_removed))
		goto err;
	if (!tdbus_match_signal(logind->bus, LOGIND_DEST, LOGIND_SESSION_IFACE,
	                        "PauseDevice", logind->session_org_path,
	                        logind, logind_bus_handle_pause_device))
		goto err;
	if (!tdbus_match_signal(logind->bus, LOGIND_DEST, LOGIND_SESSION_IFACE,
	                        "ResumeDevice", logind->session_org_path,
	                        logind, logind_bus_handle_resume_device))
		goto err;
	if (!tdbus_match_signal(logind->bus, LOGIND_DEST,
	                        LOGIND_PROPERTY_IFACE, "PropertiesChanged",
	                        logind->session_org_path, logind,
	                        logind_bus_handle_session_properties_changed))
		goto err;
	if (!tdbus_match_signal(logind->bus, LOGIND_DEST,
	                        LOGIND_PROPERTY_IFACE, "PropertiesChanged",
	                        logind->seat_path, logind,
	                        logind_bus_handle_seat_properties_changed))
		goto err;

	return true;

err:
	tw_logl_level(TW_LOG_WARN, "Failed to add signals");
	return false;

}

static bool
logind_bus_take_control(struct tw_logind_login *logind)
{
	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_SESSION_IFACE, "TakeControl",
	                            NULL, "b", false)) {
		tw_logl_level(TW_LOG_WARN, "Failed to take control session");
		return false;
	}
	return true;
}

static void
logind_bus_release_control(struct tw_logind_login *logind)
{

	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_SESSION_IFACE, "ReleaseControl",
	                            NULL, "")) {
		tw_logl_level(TW_LOG_WARN, "Failed to release session");
	}
}

static bool
logind_bus_activate(struct tw_logind_login *logind)
{
	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_SESSION_IFACE, "Activate", NULL,
	                            "")) {
		tw_logl_level(TW_LOG_WARN, "Failed to activate the session");
		return false;
	}
	tw_login_set_active(&logind->base, true);
	return true;
}

static bool
logind_bus_settype(struct tw_logind_login *logind)
{
	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_SESSION_IFACE, "SetType", NULL,
	                            "s", "wayland")) {
		tw_logl_level(TW_LOG_WARN, "Failed to set session type");
		return false;
	}

	setenv("XDG_SESSION_TYPE", "wayland", 1);
	return true;
}

/******************************************************************************
 * logind_impl
 *****************************************************************************/

static int
handle_logind_get_vt(struct tw_login *base)
{
	struct tw_logind_login *logind = tw_logind_login_from_base(base);
	return logind->vtnr;
}

static bool
handle_logind_switch_vt(struct tw_login *base, unsigned int vt)
{
	struct tw_logind_login *logind = tw_logind_login_from_base(base);

	//TODO we may only debug this through ssh
	if (logind->vtnr == vt)
		return true;
	if (!sd_seat_can_tty(logind->base.seat))
		return false;
	if (tdbus_send_method_call(logind->bus, LOGIND_DEST, logind->seat_path,
	                           LOGIND_SEAT_IFACE, "SwitchTo",
	                           NULL, "u", (uint32_t)vt)) {
		tw_logl_level(TW_LOG_WARN, "Failed to switch to vt:%d", vt);
		return false;
	}
	logind->vtnr = vt;
	return true;
}

static int
handle_logind_open(struct tw_login *base, const char *path, uint32_t flags)
{
	int fd = -1, fl;
	bool paused;
	struct stat st;
	struct tdbus_reply reply = {0};
	struct tw_logind_login *logind = tw_logind_login_from_base(base);

	if (stat(path, &st) < 0)
		return -1;
	if (!S_ISCHR(st.st_mode))
		return -1;

	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_SESSION_IFACE, "TakeDevice",
	                            &reply, "uu", major(st.st_rdev),
	                            minor(st.st_rdev)))
		return -1;

	if (!tdbus_read(reply.message, "hb", &fd, &paused)) {
		tw_logl_level(TW_LOG_WARN, "Failed to parse result from dbus");
		goto out;
	}
	//since fd is not opened by use, we need to change the fd mode
	//afterwards,
	fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		goto err_fd;
	//we have whatever the logind return to us, changing accessing mode is
	//not possible
	if (flags & O_NONBLOCK)
		fl |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, fl) < 0)
		goto err_fd;
out:
	tdbus_free_message(reply.message);
	return fd;
err_fd:
	close(fd);
	tdbus_free_message(reply.message);
	return -1;
}

static void
handle_logind_close(struct tw_login *base, int fd)
{
	struct stat st;
	int r;
	struct tw_logind_login *logind = tw_logind_login_from_base(base);

	r = fstat(fd, &st);
	close(fd);
	if (r < 0) {
		tw_logl_level(TW_LOG_WARN, "fd %d invalid", fd);
		return;
	}
	if (!S_ISCHR(st.st_mode)) {
		tw_logl_level(TW_LOG_WARN, "fd %d invalid", fd);
		return;
	}
	if (!tdbus_send_method_call(logind->bus, LOGIND_DEST,
	                            logind->session_org_path,
	                            LOGIND_SESSION_IFACE, "ReleaseDevice",
	                            NULL, "ReleaseDevice", "uu",
	                            major(st.st_rdev), minor(st.st_rdev))) {
		tw_logl_level(TW_LOG_WARN, "Failed to release logind device");
	}
}

static const struct tw_login_impl logind_impl = {
	.open = handle_logind_open,
	.close = handle_logind_close,
	.get_vt = handle_logind_get_vt,
	.switch_vt = handle_logind_switch_vt,
};

/******************************************************************************
 * initializer
 *****************************************************************************/

static char *
logind_get_session_id()
{
	int r;
	char *session_id = NULL;
	char *xdg_session_id = getenv("XDG_SESSION_ID");

	if (xdg_session_id) {
		if (sd_session_is_active(xdg_session_id) <= 0) {
			tw_logl_level(TW_LOG_WARN, "invalid XDG_SESSION_ID:%s",
			              xdg_session_id);
			return NULL;
		}
		return strdup(xdg_session_id);
	}

	r = sd_pid_get_session(getpid(), &session_id);
	if (r < 0) {
		tw_logl_level(TW_LOG_WARN, "Failed to get systemd session");
		return NULL;
	}
	return session_id;

	//TODO wlroots goes in depth for finding available sessions, we skip
	//that for now, if we are to do so, here is the steps.

	//sd_uid_get_display() to get a "primary session_id from user"
	//use sd_session_get_type verify the session_type is "wayland"
	//verify session state is active or online.
}

static bool
logind_get_seat(struct tw_logind_login *logind)
{
	int ret;
	char *seat;

        //seat
	if ((ret = sd_session_get_seat(logind->session_id, &seat)) < 0) {
		tw_logl_level(TW_LOG_WARN, "Failed to get session seat id");
		return false;
	}
	snprintf(logind->base.seat, sizeof(logind->base.seat), "%s", seat);
	if (sd_seat_can_tty(seat) > 0) {
		ret = sd_session_get_vt(logind->session_id, &logind->vtnr);
		if (ret < 0) {
			tw_logl_level(TW_LOG_WARN, "Failed to get vt number");
			goto err_vt;
		}
	} else {
		tw_logl_level(TW_LOG_WARN, "seat %s cannot tty", seat);
		goto err_tty;
	}
	free(seat);
	return true;
err_vt:
err_tty:
	free(seat);
	return false;
}

static bool
logind_get_bus(struct tw_logind_login *logind)
{
	logind->bus = tdbus_new(SYSTEM_BUS);
        if (!logind->bus) {
	        tw_logl_level(TW_LOG_WARN, "Faild to connect to D-Bus");
		return false;
	}
        if (!logind_bus_setup_dbus_path(logind))
		return false;
        if (!logind_bus_add_signals(logind))
	        return false;
        if (!(logind->event = tw_bind_tdbus_for_wl_display(logind->bus,
                                                           logind->display)))
	        return false;

	return true;
}

void
tw_login_destroy_logind(struct tw_login *login)
{
	struct tw_logind_login *logind = tw_logind_login_from_base(login);

	logind_bus_release_control(logind);

        if (logind->event)
	        wl_event_source_remove(logind->event);

        free(logind->session_id);
        free(logind->session_org_path);
        free(logind->seat_path);
        if (logind->bus)
	        tdbus_delete(logind->bus);
        tw_login_fini(login);

	free(logind);
}

struct tw_login *
tw_login_create_logind(struct wl_display *display)
{
	struct tw_logind_login *logind = calloc(1, sizeof(*logind));

	if (!logind)
		return NULL;
	if (!tw_login_init(&logind->base, display, &logind_impl)) {
		free(logind);
		return NULL;
	}
	logind->display = display;
	logind->session_id = logind_get_session_id();
	if (!logind->session_id)
		goto err;
	if (!logind_get_seat(logind))
		goto err;
	if (!logind_get_bus(logind))
		goto err;
	if (!logind_bus_take_control(logind))
		goto err;
	if (!logind_bus_activate(logind))
		goto err;
	if (!logind_bus_settype(logind))
		goto err;

	return &logind->base;
err:
	tw_login_destroy_logind(&logind->base);
	return NULL;
}

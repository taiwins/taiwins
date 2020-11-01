/*
 * login.c - taiwins login service
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

#include <wayland-server-core.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <taiwins/objects/logger.h>
#include "login.h"
#include "taiwins/backend_drm.h"

#define DEFAULT_SEAT "seat0"


static int
handle_udev_event(int fd, uint32_t mask, void *data)
{
	struct tw_login *login = data;
	struct udev_device *dev = udev_monitor_receive_device(login->mon);
	const char *action, *sysname;

	if (!dev)
		return 1;
	action = udev_device_get_action(dev);
	sysname = udev_device_get_sysname(dev);
	(void)sysname;

	if (!action || strcmp(action, "change"))
		goto out;
	//handle device?
out:
	udev_device_unref(dev);
	return 1;
}

bool
tw_login_init(struct tw_login *login, struct wl_display *display,
              const struct tw_login_impl *impl)
{
	int fd;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	const char *login_seat = getenv("XDG_SEAT");

	if (login_seat)
		strcpy(login->seat, login_seat);
	else
		strcpy(login->seat, DEFAULT_SEAT);

	wl_signal_init(&login->events.attributes_change);
	login->active = false;

	login->udev = udev_new();
	if (!login->udev) {
		tw_logl_level(TW_LOG_WARN, "Failed to create udev context");
		return false;
	}

	login->mon = udev_monitor_new_from_netlink(login->udev, "udev");
	if (!login->mon) {
		tw_logl_level(TW_LOG_WARN, "Failed to create udev monitor");
		goto err_monitor;
	}
	udev_monitor_filter_add_match_subsystem_devtype(login->mon, "drm",
	                                                NULL);
	udev_monitor_enable_receiving(login->mon);

        fd = udev_monitor_get_fd(login->mon);
        login->udev_event = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
                                                   handle_udev_event, login);
        if (!login->udev_event)
	        goto err_event;
        login->impl = impl;

	return login;

err_event:
	udev_monitor_unref(login->mon);
err_monitor:
	udev_unref(login->udev);
	return false;
}

void
tw_login_fini(struct tw_login *login)
{
	wl_event_source_remove(login->udev_event);
	udev_monitor_unref(login->mon);
	udev_unref(login->udev);
}

int
tw_login_open(struct tw_login *login, const char *path)
{
	return login->impl->open(login, path);
}

void
tw_login_close(struct tw_login *login, int fd)
{
	login->impl->close(login, fd);
}

bool
tw_login_switch_vt(struct tw_login *login, unsigned vt)
{
	return login->impl->switch_vt(login, vt);
}

int
tw_login_get_vt(struct tw_login *login)
{
	return login->impl->get_vt(login);
}

static bool
drm_device_check_kms(struct udev_device *dev, struct tw_login *login,
                     int *fd)
{
	const char *filename = udev_device_get_devnode(dev);
	const char *sysnum = udev_device_get_sysname(dev);
	drmModeRes *res;

	if (!filename)
		return false;
	if ((*fd = tw_login_open(login, filename)) < 0)
		return false;
	if (!(res = drmModeGetResources(*fd)))
		goto err_get_res;
	if (res->count_crtcs <= 0 || res->count_connectors <= 0 ||
	    res->count_encoders <= 0)
		goto err_res;
	if (!sysnum || atoi(sysnum) < 0)
		goto err_res;

	drmModeFreeResources(res);
	return true;
err_res:
	drmModeFreeResources(res);
err_get_res:
	tw_login_close(login, *fd);
	return false;
}

int
tw_login_find_primary_gpu(struct tw_login *login)
{
	int fd = -1;
	struct udev_list_entry *entry;
        struct udev_enumerate *enume = udev_enumerate_new(login->udev);

        if (!enume) {
	        tw_logl_level(TW_LOG_WARN, "failed to get udev_enumerate");
	        return -1;
        }

        udev_enumerate_add_match_subsystem(enume, "drm");
        udev_enumerate_add_match_sysname(enume, "card[0-9]*");
        udev_enumerate_scan_devices(enume);

        udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enume)) {
	        bool boot_vga = false;
	        struct udev_device *pci, *dev;
	        const char *seat, *path, *id;
	        path = udev_list_entry_get_name(entry);
	        dev = udev_device_new_from_syspath(login->udev, path);
	        if (!dev)
		        continue;

                seat = udev_device_get_property_value(dev, "ID_SEAT");
	        seat = seat ? seat : DEFAULT_SEAT;
	        if (strcmp(seat, login->seat))
		        goto next;

	        //no need to free pci
	        pci = udev_device_get_parent_with_subsystem_devtype(dev, "pci",
	                                                            NULL);
	        if (pci) {
		        id = udev_device_get_sysattr_value(pci, "boot_vga");
		        boot_vga = (id && !strcmp(id, "1"));
	        }
	        if (!boot_vga)
		        goto next;
	        if (!drm_device_check_kms(dev, login, &fd))
		        goto next;
	        udev_device_unref(dev);
	        break;

        next:
	        udev_device_unref(dev);
	        continue;
        }

        udev_enumerate_unref(enume);
        return fd;
}

void
tw_login_set_active(struct tw_login *login, bool active)
{
	if (login->active != active) {
		login->active = active;
		wl_signal_emit(&login->events.attributes_change, login);
	}

}

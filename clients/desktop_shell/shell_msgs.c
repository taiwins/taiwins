/*
 * shell_msg.c - taiwins client shell msg implementation
 *
 * Copyright (c) 2019 Xichen Zhou
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

#include <stdlib.h>
#include <string.h>

#include <strops.h>
#include <wayland-util.h>
#include "client.h"
#include "shell.h"
#include "vector.h"

static inline struct shell_notif *
make_shell_notif(const char *msg)
{
	struct shell_notif *notif;
	char *msg_dup;

	notif = malloc(sizeof (struct shell_notif));
	if (!notif)
		return NULL;
	msg_dup = strdup(msg);
	if (!msg_dup) {
		free(notif);
		return NULL;
	}
	wl_list_init(&notif->link);
	notif->msg = msg_dup;
	return notif;
}

static inline void
shell_add_notif(struct desktop_shell *shell,
                struct shell_notif *notif)
{
	if (notif && shell) {
		wl_list_insert(&shell->notifs.msgs,
		               &notif->link);
		shell_launch_notif(shell, notif);
		tw_signal_emit(&shell->notifs.msg_recv_signal,
		               notif);
	}
}

static inline void
shell_destroy_notify(struct desktop_shell *shell,
                     struct shell_notif *notif)
{
	tw_signal_emit(&shell->notifs.msg_del_signal, notif);
	wl_list_remove(&notif->link);
	free(notif->msg);
	free(notif);
}

void
shell_cleanup_notifications(struct desktop_shell *shell)
{
	struct shell_notif *pos, *tmp;
	wl_list_for_each_safe(pos, tmp, &shell->notifs.msgs, link)
		shell_destroy_notify(shell, pos);
}

static void
desktop_shell_setup_locker(struct desktop_shell *shell)
{
	//priority over another
	if (shell->transient.wl_surface &&
	    shell->transient.type == TW_APPSURF_LOCKER)
		return;
	else if (shell->transient.wl_surface)
		shell_end_transient_surface(shell);
	if (shell->widget_launch.current)
		shell_close_widget(shell);

	shell_locker_init(shell);
	tw_appsurf_frame(&shell->transient, false);
}

void
shell_process_msg(struct desktop_shell *shell,
		  uint32_t type, const struct wl_array *data)
{
	union wl_argument arg;
	struct shell_notif *notification;

	switch (type) {
	case TAIWINS_SHELL_MSG_TYPE_NOTIFICATION:
		arg.s = data->data;
		notification = make_shell_notif(arg.s);
		shell_add_notif(shell, notification);
		break;
	case TAIWINS_SHELL_MSG_TYPE_PANEL_POS:
		arg.u = atoi((char *)data->data);
		shell->panel_pos = (arg.u == TAIWINS_SHELL_PANEL_POS_TOP) ?
			TAIWINS_SHELL_PANEL_POS_TOP :
			TAIWINS_SHELL_PANEL_POS_BOTTOM;
		break;
	case TAIWINS_SHELL_MSG_TYPE_LOCK:
		desktop_shell_setup_locker(shell);
		break;
	case TAIWINS_SHELL_MSG_TYPE_SWITCH_WORKSPACE:
	{
		fprintf(stderr, "switch workspace\n");
		break;
	}
	default:
		break;
	}

}

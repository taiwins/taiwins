/*
 * shell_lock.c - taiwins client shell lock implementation
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

#include <security/pam_appl.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include <ui.h>
#include <client.h>
#include <nk_backends.h>
#include "shell.h"


static struct auth_buffer {
	struct tw_appsurf *app;
	/* struct nk_text_edit line; */
	char stars[256];
	char codes[256];
	int len;
} AUTH;

/**
 * @brief This function performs a conversation between our application and and
 * the module. It receives an array of messages in the parameters (set by the
 * module) and sets the array of responses in the parameters with the
 * appropriate content. For example if the module calls this function with a
 * message "login:" we are expected to set the firs response with the username
 *
 * @param num_msg Number of messages from the module
 * @param msg Array of messages (set by the module, should not be modified)
 * @param resp Array of responses this function must set
 * @param appdata_ptr Pointer to additional data. The pointer received is that
 * specified in the pam_conv structure below.
 */
static int
conversation(int num_msg, const struct pam_message **msgs,
	     struct pam_response **resp, void *appdata_ptr)
{
	struct pam_response *arr_response =
		malloc(num_msg * sizeof(struct pam_response));
	//we will be asked for password here
	*resp = arr_response;
	for (int i = 0; i < num_msg; i++) {
		arr_response[i].resp_retcode = 0;
		switch (msgs[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
		case PAM_PROMPT_ECHO_ON:
		{
			arr_response[i].resp =
				(char *)malloc(strlen(AUTH.codes)+1);
			strcpy(arr_response[i].resp, AUTH.codes);
		}
		break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			break;
		}
	}
	return PAM_SUCCESS;
}

//this is a idle event
static int run_pam(struct tw_event *event, int fd)
{
	struct passwd *passwd = getpwuid(getuid());
	char *username = passwd->pw_name;
	struct tw_appsurf *app = event->data;
	struct desktop_shell *shell =
		container_of(app, struct desktop_shell, transient);
	int retval = 0;
	const struct pam_conv conv = {
		.conv = conversation,
		.appdata_ptr = NULL,
	};
	pam_handle_t *auth_handle = NULL;

	if ((retval = pam_start("i3lock", username, &conv, &auth_handle))
	    != PAM_SUCCESS)
		goto locked;
	if (retval == PAM_SUCCESS)
		retval = pam_authenticate(auth_handle, 0);
	if (pam_end(auth_handle, retval) != PAM_SUCCESS)
		goto locked;
	if (retval == PAM_SUCCESS)
		shell_end_transient_surface(shell);
locked:
	memset(AUTH.stars, 0, 256);
	memset(AUTH.codes, 0, 256);
	AUTH.len = 0;
	return TW_EVENT_DEL;
}


static void
shell_locker_frame(struct nk_context *ctx, float width, float height,
		   struct tw_appsurf *locker)
{

	struct passwd *passwd = getpwuid(getuid());
	const char *username = passwd->pw_name;

	//so we need roughly 200 by 50
	float x = width / 2.0 - 100.0;
	float y = height / 2.0 - 30.0;
	float ratio[] = {0.9f, 0.1f};
	int clicked = false;

	//to input password, we need U+25CF(black circle), for now we just use
	//'*'
	if (nk_begin(ctx, "LOGIN", nk_rect(x, y, 200, 60),
		    NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 25, 1);
		nk_label(ctx, username, NK_TEXT_CENTERED);
		nk_layout_row(ctx, NK_DYNAMIC, 25, 2, ratio);
		//so this line already copies data to command buffer at this
		//line. you will see stars frame/maybe on releasing event.
		nk_edit_string(ctx, NK_EDIT_SIMPLE, AUTH.stars,
			       &AUTH.len, 256, nk_filter_ascii);
		clicked = nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_RIGHT);
	} nk_end(ctx);
	if (AUTH.stars[AUTH.len - 1] != '*') {
		AUTH.codes[AUTH.len - 1] = AUTH.stars[AUTH.len - 1];
		AUTH.stars[AUTH.len - 1] = '*';
	}
	//we need to swap out the buffer and copy the last char to
	if (nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER) || clicked) {
		struct tw_event e = {
			.data = locker,
			.cb = run_pam,
		};
		tw_event_queue_add_idle(&locker->tw_globals->event_queue, &e);
	}
}


void shell_locker_init(struct desktop_shell *shell)
{
		/* widget_should_close(&shell->widget_launch, NULL); */
	struct wl_surface *wl_surface =
		wl_compositor_create_surface(shell->globals.compositor);
	shell->transient_ui =
		tw_shell_create_locker(shell->interface, wl_surface, 0);
	struct shell_output *output = shell->main_output;

	tw_appsurf_init(&shell->transient, wl_surface,
			 &shell->globals, TW_APPSURF_LOCKER,
			 TW_APPSURF_NORESIZABLE | TW_APPSURF_COMPOSITE);
	nk_cairo_impl_app_surface(&shell->transient, shell->widget_backend,
				  shell_locker_frame, output->bbox);

	memset(AUTH.stars, 0, 256);
	memset(AUTH.codes, 0, 256);
	AUTH.len = 0;
}

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


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"


static struct auth_buffer {
	struct app_surface *app;
	struct nk_text_edit line;
	char words[256];
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
		{//givin back passwords
			/* const char *msg_content = msgs[i]->msg; */
			char pass[100] = "blahblahblan";
			arr_response[i].resp = (char *)malloc(strlen(pass)+1);
			strcpy(arr_response[i].resp, pass);
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
	struct app_surface *app = event->data;
	int retval;
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
	if (retval == PAM_SUCCESS)
		retval = pam_acct_mgmt(auth_handle, 0);
	if (pam_end(auth_handle, retval) != PAM_SUCCESS)
		goto locked;
	if (retval == PAM_SUCCESS)
		app_surface_release(app);
locked:
	return TW_EVENT_DEL;
}


static void
shell_locker_frame(struct nk_context *ctx, float width, float height,
		   struct app_surface *locker)
{

	struct passwd *passwd = getpwuid(getuid());
	const char *username = passwd->pw_name;
	static char stars[256];

	//so we need roughly 200 by 50
	float x = width / 2.0 - 100.0;
	float y = height / 2.0 - 25.0;


	if (nk_begin(ctx, "LOGIN", nk_rect(x, y, width, height),
		    NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 20, 1);
		nk_label(ctx, username, NK_TEXT_ALIGN_MIDDLE);
		nk_layout_row_dynamic(ctx, 25, 1);
		nk_edit_buffer(ctx, NK_EDIT_FIELD, &AUTH.line, nk_filter_default);
		//we have to replace
		/* /\* nk_textedit_text(&passwords, const char *, int total_len) *\/ */
		/* nk_edit_buffer(ctx, nk_flags, &passwords, NULL); */
	}
}


void shell_locker_init(struct desktop_shell *shell)
{
		/* widget_should_close(&shell->widget_launch, NULL); */
	struct wl_surface *wl_surface =
		wl_compositor_create_surface(shell->globals.compositor);
	struct tw_ui *locker_ui =
		tw_shell_create_locker(shell->interface, wl_surface, 0);
	struct shell_output *output = shell->main_output;

	app_surface_init(&shell->transient, wl_surface, (struct wl_proxy *)locker_ui,
			 &shell->globals, APP_SURFACE_LOCKER, APP_SURFACE_NORESIZABLE);
	nk_cairo_impl_app_surface(&shell->transient, shell->widget_backend,
				  shell_locker_frame, output->bbox);

	nk_textedit_init_fixed(&AUTH.line, AUTH.words, 256);
}


#pragma GCC diagnostic pop

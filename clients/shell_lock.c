#define _POSIX_C_SOURCE 200809L
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

struct auth_buffer {
	struct app_surface *app;
	char passwd[256];
};

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
			const char *msg_content = msgs[i]->msg;
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
	//so we need roughly 200 by 50
	float x = width / 2.0 - 100.0;
	float y = height / 2.0 - 25.0;

	static struct nk_text_edit passwords;

	if (nk_begin(ctx, "LOGIN", nk_rect(x, y, width, height),
		    NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 30, 1);
		/* nk_textedit_text(&passwords, const char *, int total_len) */
		nk_edit_buffer(ctx, nk_flags, &passwords, NULL);
	}
}

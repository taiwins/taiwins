#include <stdio.h>
#include <compositor.h>
#include "input.h"



//first keybinding
struct tw_keypress kp_quit[1] = {
	{
		.keysym = XKB_KEY_q,
		.keycode = KEY_Q,
		.modifiers = MODIFIER_SUPER,
	},
};

vector_t kp_q = {
	.elems = kp_quit,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_quit),
	.alloc_len = num(kp_quit),
};


struct tw_keypress kp_emacs_openfile[2] = {
	{
		.keysym = XKB_KEY_x,
		.keycode = KEY_X,
		.modifiers = MODIFIER_CTRL,
	},
	{
		.keysym = XKB_KEY_f,
		.keycode = KEY_F,
		.modifiers = MODIFIER_CTRL,
	}
};

vector_t kp_of = {
	.elems = kp_emacs_openfile,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_emacs_openfile),
	.alloc_len = num(kp_emacs_openfile),
};


struct tw_keypress kp_emacs_closewin[2] = {
	{
		.keysym = XKB_KEY_x,
		.keycode = KEY_X,
		.modifiers = MODIFIER_CTRL,
	},
	{
		.keysym = XKB_KEY_c,
		.keycode = KEY_C,
		.modifiers = MODIFIER_CTRL,
	}
};

vector_t kp_cf = {
	.elems = kp_emacs_closewin,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_emacs_closewin),
	.alloc_len = num(kp_emacs_closewin),
};


struct tw_keypress kp_emacs_lastbuff[2] = {
	{
		.keysym = XKB_KEY_x,
		.keycode = KEY_X,
		.modifiers = MODIFIER_CTRL,
	},
	{
		.keysym = XKB_KEY_b,
		.keycode = KEY_B,
	}
};

vector_t kp_lb = {
	.elems = kp_emacs_lastbuff,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_emacs_lastbuff),
	.alloc_len = num(kp_emacs_lastbuff),
};

struct tw_keypress kp_emacs_bufflist[2] = {
	{
		.keysym = XKB_KEY_x,
		.keycode = KEY_X,
		.modifiers = MODIFIER_CTRL,
	},
	{
		.keysym = XKB_KEY_b,
		.keycode = KEY_B,
		.modifiers = MODIFIER_CTRL,
	}
};

struct tw_keypress kp_emacs_readonly[2] = {
	{
		.keysym = XKB_KEY_x,
		.keycode = KEY_X,
		.modifiers = MODIFIER_CTRL,
	},
	{
		.keysym = XKB_KEY_q,
		.keycode = KEY_Q,
		.modifiers = MODIFIER_CTRL,
	}
};




vector_t kp_bl = {
	.elems = kp_emacs_bufflist,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_emacs_bufflist),
	.alloc_len = num(kp_emacs_bufflist),
};

vector_t kp_ro = {
	.elems = kp_emacs_readonly,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_emacs_readonly),
	.alloc_len = num(kp_emacs_readonly),

};


struct tw_keypress kp_x86audioup[] = {
	{
		.keysym = XKB_KEY_XF86AudioRaiseVolume,
		.keycode = KEY_VOLUMEUP,
	}
};
struct tw_keypress kp_x86audiodw[] = {
	{
		.keysym = XKB_KEY_XF86AudioLowerVolume,
		.keycode = KEY_VOLUMEDOWN,
	}
};

struct tw_keypress kp_x86audioplay[] = {
	{
		.keysym = XKB_KEY_XF86AudioPlay,
		.keycode = KEY_PLAY,
	}
};


struct tw_keypress kp_x86audiopause[] = {
	{
		.keysym = XKB_KEY_XF86AudioPause,
		.keycode = KEY_PAUSE,
	}
};

struct tw_keypress kp_x86audionext[] = {
	{
		.keysym = XKB_KEY_XF86AudioNext,
		.keycode = KEY_NEXT,
	}
};

struct tw_keypress kp_x86audioprev[] = {
	{
		.keysym = XKB_KEY_XF86AudioPrev,
		.keycode = KEY_PREVIOUS,
	}
};





vector_t kp_audioup = {
	.elems = kp_x86audioup,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_x86audioup),
	.alloc_len = num(kp_x86audioup)
};

vector_t kp_audiodw = {
	.elems = kp_x86audiodw,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_x86audiodw),
	.alloc_len = num(kp_x86audiodw)
};


vector_t kp_audiopl = {
	.elems = kp_x86audioplay,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_x86audioplay),
	.alloc_len = num(kp_x86audioplay)
};

vector_t kp_audiops = {
	.elems = kp_x86audiopause,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_x86audiopause),
	.alloc_len = num(kp_x86audiopause)
};

vector_t kp_audionx = {
	.elems = kp_x86audionext,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_x86audionext),
	.alloc_len = num(kp_x86audionext)
};

vector_t kp_audiopv = {
	.elems = kp_x86audioprev,
	.elemsize = sizeof(struct tw_keypress),
	.len = num(kp_x86audioprev),
	.alloc_len = num(kp_x86audioprev)
};


void func_quit(void)
{
	fprintf(stderr, "testing shortcut, quiting\n");
}

void func_of(void)
{
	fprintf(stderr, "testing shortcut: open file\n");
}

void func_cf(void)
{
	fprintf(stderr, "testing shortcut: close file\n");
}

void func_lb(void)
{
	fprintf(stderr, "testing shortcut: last buffer\n");
}

void func_bl(void)
{
	fprintf(stderr, "testing shortcut: buffer list\n");
}
void func_ro(void)
{
	fprintf(stderr, "testing shortcut: read only\n");
}


void func_audioup(void)
{
	fprintf(stderr, "testing shortcut: audio up\n");
}

void func_audiodw(void)
{
	fprintf(stderr, "testing shortcut: audio dw\n");
}

void func_audio(void)
{
	fprintf(stderr, "testing shortcut: xf86 audio related\n");
}

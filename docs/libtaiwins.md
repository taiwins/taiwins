## Libtaiwins architecture.

The easiest way to draft a wayland compositor using **libtaiwins** is by
looking at the
[x11-test.c](https://github.com/taiwins/taiwins/tree/master/test/x11-test.c),
[wayland-test.c](https://github.com/taiwins/taiwins/tree/master/test/wayland-test.c)
and
[drm-test.c](https://github.com/taiwins/taiwins/blob/master/test/drm-test.c). You
will see that it takes as few as 100 lines of code to run a compositor (with
some help of course). On the end end we have `tw_object` implements the wayland
object in C, on the other end we have `tw_backend` for abstracting
hardwares. The `tw_engine` is the middle man connects them together.  together,
it takes input events from backends and forward it to the `wl_seat`, sets
correct output mode you asked in the backend and helps initializing the
`tw_render_context`. The `tw_render_context` handles the GPU resources and
provides drawing interfaces we called *pipeline*.  Other subsystems like
`tw_shell` is an taiwins protocol implementation.

Thanks to the modular design of the libtaiwins, most of the subsystems rarely
needs to know care about others, they interface with wayland object using
[twobjects](https://github.com/taiwins/twobjects) except `tw_backend`, which is
completely unaware of its users.

Since a picture talks louder than thousands words, an accurate enough
description of libtaiwins architecture being here:

![](./imgs/libtaiwins-arch.svg)

## Get into details

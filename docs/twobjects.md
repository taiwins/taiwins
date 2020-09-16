# Twobjects quick reference

Twobjects is the result of migration taiwins from **libweston**, as libweston
has many constrains, lacks of important features and has several severe bugs for
daily users. Firstly I looked into **wlroots** and found it was not modular
enough and the design did not fit the taiwins, inspired by the two prior
project, I created this library for faciliting the development of the wayland
compositor.

## Features

- twobjects implements all the interfaces in the `wayland.xml` and several
		important protocols such as `xdg-shell` and `linux-dmabuf`.
- twobjects is designed to backend agnositc, you can stack it on top of other
		backend implementation such as wlroots.
- twobjects is designed to be autonomous. Unlike wlroots approach, which heavily
  exposes `wl_signal` from protocols so the users still has to write a layer of
  glue to use them. In twobjects, many of wayland protocols manages itself thus
  transparent to the backend developer. A good example is `wl_data_device`.
- twobjects has built-in logging and profiling system and other utilites.

## Building

Install dependencies:

* meson
* wayland
* wayland-protocols
* xkbcommon
* libdrm 
* pixman

Commands for build:

	meson build
	ninja -C build

Commands for install:

	sudo ninja -C build install

## Getting started

Many protocols essentially implements a `wl_global` object like `wl_compositor`
in the compositor, twobjects typically implements a corresponding type like
`tw_compositor`. You can create the static global by using function like
`tw_compositor_create_global` or allocate the memory yourself then use
`tw_compositor_init`. 

There are some cases where twobjects abstracts several protocols into common
interface for users. For example, the `tw_desktop_manager` implements both
`wl_shell` and `xdg_shell` and exposes `tw_desktop_surface` for the user, this
is similar to the design of libweston.

For "middle-man" protocols like `wl_data_device`, the internals or managing
`wl_data_source`, `wl_data_offer` is transparent to user. Users simply
initialize the `tw_data_manager` for getting the clipboard in wayland.

## Twobjects utilities

Twobjects implements utilies like matrix, cursor, logging and profiling
functions for users. Check out the headers for usage.

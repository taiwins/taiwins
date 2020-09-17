# Taiwins Documentation				{#mainpage}

This manual documents the API and design of the Taiwins. Several notes were
registered along the development. Taiwins consists of serveral **sub-projects**
like **twclients** and **twobjects** for distinctive parts, those libraries are
embedable and can be used independently outside serves general building blocks
for wayland projects.

### Sub-projects

- [twclient](https://github.com/taiwins/twclient) : general purpose wayland
  client widget system. **Twclient** implements many common
  `wl_*_listeners` and provides convinient interfaces to implement shmbuf
  `wl_surface` or EGL `wl_surface`. You can take a look at the [twclient
  guide](api.md) for getting started.

- [twobjects](https://github.com/taiwins/twobjects) : wayland server protocols
  implementation. It implements all the basic wayland protocols a compositor has
  to have as well as many other popular ones. It is designed to be modular and
  autonomous, check out the [quick reference](twobjects.md) for getting started.

- [taiwins-protocols](https://github.com/taiwins/taiwins-protocols) : collection
  of taiwins wayland protocols.

### Other related notes

If you are new to wayland, you may be also interested in the [wayland note](wayland.md)
for some important bits, I also recorded parts of the reference compositor
weston [weston](https://gitlab.freedesktop.org/wayland/weston), for some
insights of designing a wayland compositor.

[lua](lua.md) notes is the compact summary on using the lua c API.

Several taiwins design sketches can also be found here.

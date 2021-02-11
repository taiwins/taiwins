# Taiwins Documentation				{#mainpage}

This manual documents the API and design of the Taiwins. Several notes were
registered along the development. Taiwins consists of serveral **sub-projects**
like **twclients** and **twobjects** for distinctive parts, those libraries are
embedable and can be used independently outside serves general building blocks
for wayland projects.

## Sub-projects

- [twclient](https://github.com/taiwins/twclient) : general purpose wayland
  client widget system. **Twclient** implements many common
  `wl_*_listeners` and provides convinient interfaces to implement shmbuf
  `wl_surface` or EGL `wl_surface`. 

- [twobjects](https://github.com/taiwins/twobjects) : the server-side wayland
  objects implementation. It implements all the basic types a compositor has to
  have, such as `wl_surface`, as well as many other popular ones like
  `linux_dmabuf`. It is designed to be modular and autonomous, check out the
  [quick reference](twobjects.md) for getting started.

- [taiwins-protocols](https://github.com/taiwins/taiwins-protocols) :
  collection of wayland protocols specific to taiwins.

## Taiwins design

Taiwins is a collection of the compositor (taiwins), the wayland compositor
library(libtaiwins), and useful clients like the shell (taiwins-shell) and the
application launcher(taiwins-console).

### Server-side design

Take a look [here](libtaiwins.md)

### Client-side design

Take a look [here](libtwclient.md).

## Other notes

If you are new to wayland, you may be also interested in the [wayland
note](wayland.md) for some important bits. [lua](lua.md) notes is the compact
summary on using the lua c API. Taiwins also uses dbus for configuration and
various other things, a quick tutorial on dbus can be found [here][dbus.md].


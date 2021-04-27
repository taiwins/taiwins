## Taiwins Documentation				{#mainpage}

Welcome to the tawins documentation. This document describes the Architecture
and APIs of Taiwins for developers. The documents were recorded during the
various stages of taiwins development.

## Sub-projects

Taiwins is consists of multiple subprojects besides the main libtaiwins
library. They plays different roles in the taiwins ecosystem and they can also
function indepedently of taiwins as well.

- [twclient](https://github.com/taiwins/twclient) is the general purpose
  wayland client library. It was created to handle the first 500 lines of
  wayland code before you can draw a `wl_surface`. Twclient uses an event queue
  to process incomming events like IO, timer and idle events, it is similar to
  what is done in the wayland server end. Most of interfaces in wayland client
  protocols are implemented and it handles the interfaces to implement a shmbuf
  `wl_surface` or EGL `wl_surface`.
  
  An optional **twclient-icons** library comes with **twclient** for collecting
  xdg desktop elements like icons, cursor and application entries.

- [twidgets](https://github.com/taiwins/twidgets) is a widget drawing library
  built upon twclient. It is intented to use as a embedded library in your
  application. [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) is the
  GUI system of choice. Taiwins itself use the library for creating desktop
  widgets.

- [taiwins-protocols](https://github.com/taiwins/taiwins-protocols) is the
  collection of wayland protocols specific to taiwins.

- [tdbus](https://github.com/xeechou/tdbus) is a tiny D-Bus communication
  library for much simpler interface than raw `DBusConnection`.

# Related Pages

- [Libtaiwins introduction](libtaiwins.rst)
- [wayland introduction](wayland.rst)
- [Twclient quick reference](libtwclient.rst)
- [Twobject quick reference](twobjects.rst)
- [D-Bus API Quick Reference](dbus.rst)
- [Lua C API quick guide](lua.rst)
- [Xwayland in taiwins](xwayland.rst)
- [Taiwins window manager](window_management.rst)

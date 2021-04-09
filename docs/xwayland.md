## Implementing Xwayland WM

Xwayland wm, or xwm, is a window manager module inside wayland compositor, it
talks to the Xwayland server and mapping the logic of xwindow to a desktop
wl_surface. Here in taiwins, we implements the `tw_desktop_surface` logic using
xwindow and talk to the actual WM using the `tw_desktop_surface_api`.

In general, it follows a few fixed step:

1. creating `/tmp/.X#-lock` and X sockets(unix and abstract).
2. Writing the PID of the compositor to `/tmp/.X#-lock`.
3. exec the Xwayland instance and let it listen to the X sockets.
4. Creating `root_window` and set the `substructure_redicrect` attribute.
5. creating a `wl_event_resource` for the socket and handle the events.

### X window architecture.

In X, windows are in the tree, we have a root window, and every one else lives
under it. Historical reason, X windows assume they can set and change its
position by it self, and the old Xwm simply do that. Later we have the
`substructure_redirect` attributes available, it intercepts the commands of X
windows and let wm handles it differently. A Xwm is simply a `root_window`
intercepting subwindow's command. This is different than wayland, where you have
a `wayland-client` and `wayland-server` concept. In X, both wm and clients uses
the same library. 

### Xwayland architecture.
A Xwayland is a drop-in replacement of the Xserver, X clients won't feel a bit
of difference, at the same time, it is also a wayland client to the wayland
compositor. It maps a `xcb_window` to a `wl_surface` so the compositor can
treat the X clients no differently. Basically a few moments after the creation
of a X window, we would hear a client message advertise the `wl_surface` id of
this window from the Xwayland. The server uses this id for finding the
`wl_resource` for the surface, note that this generally happens before
`wl_resource` is created on the server.

### Handling the event loop
When handling the `wl_event_resource` we mentioned above, the code ususally
looks like this:

```
while(event = xcb_poll_event(xcb_connection)) {
	switch (event->response_type) {
	case XCB_CREATE_NOTIFY:
		...;
		break;
	case XCB_DESTROY_NOTIFY:
		...;
		break;
	case XCB_CONFIGURE_REQUEST:
		...;
		break;
	case XCB_CONFIGURE_NOTIFY:
		...;
		break;
	case XCB_MAP_REQUEST:
		...;
		break;
	...
	}
}

```

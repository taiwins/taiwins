# wayland objects
	The objects creation/destruction should be done with only requests.
	- creation:
		- you can use `new_id` in the `request`, this is the usual way, returns
		  an object, like `wl_surface` creation from `wl_compositor`.
		- having a `new_id` in `event`, passed as an argument, I am supposed to
		  store it. Said not to use it if unnecessary.
	- destruction:
		- `wl_foobar_destroy` request. Usual way, server receives it and have a
		  signal for that.
		- create an `event` and document it for the client to destroy the
		  proxy. Like `wl_callback`. The obvious difference is that the protocol
		  has no request at all! Said by PQ in the
		  [blog](http://ppaalanen.blogspot.com/2014/07/wayland-protocol-design-object-lifespan.html).


# weston objects
	- `weston_surface` is the `wl_resource` of `wl_surface`
	- `weston_output` is the `wl_resource` of the `wl_output`.

# weston_output_repaint_loop and discovered problem.
	The surface has to follow the `frame->attach->damage->commit->done` loop!
	If done happen after next commit, the client will having trouble waiting the
	done event for the corresponding frame, it lags the rendering.
## if you want to unmap the view
	It is okay if all the operation is transparent to client. The compositor
	suddenly unmap the view and remap the view, clients just get paused, nothing
	wrong happens.

# wayland-server implementations
## event-loop
	`event_loop` is like what I have in the `client.c`, it is almost implemented
	in the same way, it has the `timer`, `idle`, `fd` (It uses `pipe2` for
	receiving data instead of `inotify`).
	- `idle`  :
	- `timer` :
## wl_client_create
	This is the only time you need to explicitly create a client for the given
	socket, otherwise the clients are created by wayland protocols(I think the
	socket is created when the first client created(if you look into the
	`wl_client_create` code)). Since you are forced to give a `fd`, the only way
	is providing a `socketpair`.

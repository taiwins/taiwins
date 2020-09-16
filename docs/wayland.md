This page contains various notes about wayland objects and weston
implementation. Note that since taiwins is not using **libweston** anymore, the
knowledge maybe outdated. The notes is written based on my understanding and I
am not a weston developer, thus there could be various misinterpretations.

# Wayland notes
## wayland objects
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
	In one phrase: created by clients, destroyed by clients; created by server,
		  disposed by server.

## wayland-server implementations
### event-loop
	`event_loop` is like what I have in the `client.c`, it is almost implemented
	in the same way, it has the `timer`, `idle`, `fd` (It uses `pipe2` for
	receiving data instead of `inotify`).
	- `idle`  :
	- `timer` :
### wl_client_create
	This is the only time you need to explicitly create a client for the given
	socket, otherwise the clients are created by wayland protocols(I think the
	socket is created when the first client created(if you look into the
	`wl_client_create` code)). Since you are forced to give a `fd`, the only way
	is providing a `socketpair`.x

# weston notes
## weston objects
	- `weston_surface` is the `wl_resource` of `wl_surface`
	- `weston_output` is the `wl_resource` of the `wl_output`.

### weston_layer
`weston_layer`s are stored from top to bottom in the compositor, in that case
`compositor->layer_header->top_layer->...bottom_layer`. The same priority
pattern applies within the layer. So the top view is the first view in the
layer's `view_link`. When compositor starts painting, it will build the
view_list from all the layers(priority top to down). With the information, the
compositor can figure out which parts of the `weston_plane` (a `pixman_region`)
are damaged.

### weston_plane
The `weston_plane` is a logical representation of the hardware compositing
planes, so we only need to do the compositing on every plane, and the hardware
composits the different planes. Currently here is a piece of code inside the
`gl-renderer` and `pixman-renderer`.

	wl_list_for_each_reverse(view, compositor->view_list, link)
		if (view->plane == &compositor->primary_plane)
			draw_view(view);

## weston_output_repaint_loop and discovered problem.
	The surface has to follow the `frame->attach->damage->commit->done` loop!
	If done happen after next commit, the client will having trouble waiting the
	done event for the corresponding frame, it lags the rendering.
### if you want to unmap the view
	It is okay if all the operation is transparent to client. The compositor
	suddenly unmap the view and remap the view, clients just get paused, nothing
	wrong happens.

## weston coordinate system.
	weston should have 3 different coord system, since they are all 2D, it
	shouldn't be hard.
### compositor space (global space, it is global ).
### output space
### view space (globalx - view->geometry.x).
	related function:
	- `weston_view_to_global_{fixed,float,int}` :: map a local view coordinate
	  to global coordinate.
	- `weston_view_from_global_{fixed,float,int}` :: map a global coordinate to
	  view coordinate.
	- `weston_compositor_pick_view` :: search the view in the view list and
	  return the local coordinate of the view.
###  weston_pointer
	It has `x,y`, `grab_x, grab_y`, `sx,sy` 3 coordinates, the last one for its
	focused view, you can use
	`weston_view_from_global_fixed(pointer->focused,
		pointer->x, pointer->y, &pointer->sy)` if neccessary.

### Weston-desktop API

The API reassembles the `xdg_shell_protocols` protocol, but it internally glued the
`wl_shell`,`xdg_shell_v5` and `xdg_shell_v6` under one implementation, by
exposing a common API, a compositor can drive multiple desktop protocols.

### wl_frame
The key function is `weston_output_repaint`. You will see the accumulation of
`frame_callback`, `weston_compositor_build_view_list`, and call to
`renderer->repaint`, then send done to all the `frame_callback`.

### weston-renderer implementation
The one key function to look at is the `weston_output_repaint`. This is the
frame callback, called in maybe `eventloop`. It does a few things:

- it need to build up the `view_list` of current frame, extract from layers, and
  sort them in order.

- move views to planes, if backend supports multiple planes, it does that.

- initialize the `frame_callback` list, this is where the `frame_done` gets sent

- next it accumenate the damage, when you set `weston_view_damage_ below`, it
  gets accumulated here.

- Then it calls renderer to repaint.

- sends all the `frame_callbacks`.

- output has an animation list. It does that as well.

### weston-view coordinates
see the `compositor_accumulate_damamge`, there is nothing about output
weston_view has a geometry, this geometry is in global coordinates.

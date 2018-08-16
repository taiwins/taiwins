# Weston-desktop API

The API essentially implements the `xdg_shell_protocols`, it combines the
`wl_shell`,`xdg_shell_v5` and `xdg_shell_v6` under one implementation, which is
really smart, saves much resources for the developers.

## weston_layer
`weston_layer`s are stored from top to bottom in the compositor, in that case
`compositor->layer_header->top_layer->...bottom_layer`. The same priority
pattern applies within the layer. So the top view is the first view in the
layer's `view_link`. When compositor starts painting, it will build the
view_list from all the layers(priority top to down). With the information, the
compositor can figure out which parts of the `weston_plane` (a `pixman_region`)
are damaged.

## weston_plane
The `weston_plane` is a logical representation of the hardware compositing
planes, so we only need to do the compositing on every plane, and the hardware
composits the different planes. Currently here is a piece of code inside the
`gl-renderer` and `pixman-renderer`.

	wl_list_for_each_reverse(view, compositor->view_list, link)
		if (view->plane == &compositor->primary_plane)
			draw_view(view);

## the pixman_region_t
`pixman_region16_t` or `pixman_region32_t` are both regions, because it contains
boxes(x,y,w,h). You have to walk though the pixman examples to know how to use
it.


## Taiwins-desktop implementation
desktop has following concepts:
	- **workspace**  one workspace contains all the views from different **output**.
	- **layer** tiling layer, floating layer, hidden layer, we probably doesn't
	  need hidden layer now.
	- **layout** can work on per **output** space or directly under
	  compositor.

Desktop is the center of work now, it is hard to organize than others. The
dilemma here is how we can implement the tiling, do we go for the container
option like i3? How can we organize the data structure. We have to disposer
every view, what kind of the iteration information we need for that? An iterator
object? For exemple, master layout can just record the index and column
size. The container based layout need to store the tree, it is much more
complicated.

We are getting the views into a array, then pass that every time to the layout
algorithm. We would want it to be a tree that works with list as well, but we
cannot be sure, the array has advantages we can store them on stack, but it is
minimum so we cannot take advantage too much of the tree. Can we use the heap
structure then use the in place sort?

If we want to make it to a tree, what kind of tree is needed? Radix tree?

### sample process
	- adding a window in the workspace
		* decide to put in the floating layer or tiling layer.
		* you need to have the output from `tw_get_focused_output`.
		* find the **layout** corresponded to the **output**.
		* decide how you want to tell the **layout** new view is ready.
		* `disposer` all the views in the layout or just the one.
	- deleting a window.
		* decide if it is in the floating/tiling layer
		* decide how to announce the deleting of the view.
		* `disposer` all the views or not.
	- focus the window.
		* throw it to the top of the view_list.
	- moving up or down from the current view.
		* modify the positin of the view in the tree?

So for the `weston_desktop_surface`, it cares only the setting position and size
of the desktop_surface. For the layout, it cares more in detail about the state
of the tree, if new views is insert/delete? moving up or not? So it maybe a good
idea to implement an input and output interface. With this pattern, we can
unified the interface of floating layout and tiling layout. Some of the event is
not valid to the floating mode and vice versa. This is the data driving
programming I guess.

	//at event point.
	desktop.make_event(event_type, view) >> layout;
	layout.prod_layout() >> desktop;


### tiling and floating

### disposing algorithm
Another piece of problem is that when we launch the application, we have no idea
where to put them, the size of the it and many other parameters. We address this
problem with a launcher problem, how we can do that? The details are not really
interesting I guess, but the idea is using a `shared_memory` with server. When
we launch the program, we decide the parameters with the `launcher`, the
`launcher` writes them into the buffer, so when the program really gets
created. The server can decide it by read the buffer.

## weston-renderer implementation
TODO

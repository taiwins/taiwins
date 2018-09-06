# Taiwins-shell architecture

The `taiwins-shell` is the shell interface that interacts with the ui layer
views in the `taiwins-server`. It implements the **arriere plane**, **widget and
activitiy panel** and **application launcher**. Maybe even the
**verrou(casier)**. All the functionalities are expected being done in one
application.

# The unified event processor
Previously we have two threads to process events using linux `inotify` APIs, it
is dull. Finally we have switch to a unfied `tw_event_queue` interface that
deals with both events from `wl_display` and other types events like
`timer`. Grace a the unix **everything is a file** philosophy, we treat time,
`wl_display` and other things all like files and an `epoll` waits them all. This
design will probably not change for a while(we lacks of error handling when
wl_display closes).

Look the design before, it is so ugly...

	poll()
	for event in leftovers:
		event.do()
	for event in local_events:
		if event in inotify_events and event.triggered():
			if event.resource_available()
				event.do()
			else:
				event.queue()
		else if event in timeout_events and event.timeout():
			event.do()

	for event in proxy_events:
		event.do()

# app-surface

This struct was designed for all the wayland client which requires a
`wl_surface`. Right now we have three different types of `app-surface`:
background, panel and widgets. It could be double buffered, EGL based or maybe
just writing to a pixel buffer(maybe some rework).

## backend
- cairo 2D widget drawing for icon.
- EGL with nuklear backends.

## ui-layer positioning and size problem
I face different cases here for positioning:
- shell widgets determines the position by itself(where you click it), and the
  size also.
- launcher's position is determined by server(a fixed position), the size is
  determined by itself.
- panel's position is determined by server, the size is determined by server,
  the background is same as panel.
- the widget icons determines the position and size from its parents(panel).
- lockers probably is the as panel and background.
- maybe virtual keyboards.

  Now we have created a unfied protocl `tw_ui`, it does the trick, creating
  `tw_ui` from panel, background and all that, the widget want the pos, when we
  create `tw_ui` with the position. Fairly simple

### the launch and close of the widget
the launch could be triggered by input(click and keypresses). But closing them
is tricky, for widgets, closing happens when we lose inputs, by clicking other
places. For launcher, it happens when we press enter and sends the close
reques. For panel and background, we also send the close request when we receive
the death of `wl_output`. It would be nice if we have only one interface(for
example `tw_shell_ui`) and uses launch and close request (Or simply by
destroying the object) so server cleans it up.


### resizing

Des la we have been avoiding the problem of resizing for `app_surface`, it is
problematic.

Actually a solution to this overlapping is necessary in a general the UI
designs, when we are in parent UI and want to access one of the children UI, we
need an access point, like a button, and this buffer can inhabit inside its
parent or itself, since we chooses the later, we need to let the parent know how
to access it. The current solution towards the problem is by three callbacks.

 - the parent provide a `paint_subsurface` callbacks for the children.
 - But it has no pointers to the children. This causes a problem because:
 - the second children my need to know the first children to decide where it
   sits as well. There are several solution of arrange children.
   - vector or list. It may or may not have random access.
   - quad-tree or binary-tree depends on if the application is 1D or 2D. (Damn
	 it, Je aurait du choisir C++)
 - Now we don't have any other choices except making methods in
   `app_surface`. The APIs exposed should be,
   - `find_subapp_at_xy`
   - `n_subapp`: number of subapps.
   - `add_new_subapp`, in panel's case, it is creating new subapp.
   I cannot provide an random access method or iterating method because we don't
   know how the layout of subapps.

### panel
The panel is the most tricky one, it reflexes the hierarchy structure, the
widgets are hooked on it. It has commit events every time when the widget has
updates on icons. It is almost the entire reason that I had this
`client-side-event-processor`.

### widgets
Widget sits on the `WESTON_LAYER_POSITION_UI` layer, where panel also sits. So
in that sense we cannot remove all the all the views in the panel anymore. When
we click on the icon, one widget should appear, itself need to decide where it
should appear, because compositor has no idea about that. In that case, there
need to be a `wl_request` to draw the app. Normally you will draw the EGL stuff
on a `wl_shell_surface` provided by the wayland api. what we need to do is
similar `taiwins_set_widget(widget, x, y)`. The widgets are implemented with
**Immediate UI** methods, that is, we are taking advantage of the opengl drawing
method to draw every frame. With it we don't need to create the graphic resource
for every widgets. But at the same time, we have to take care all that `EGL`,
`OpenGL` setup.

### eglapp and eglenv
Creating EGL context is not really hard. Only problem is that we can only
compile `OpenGL` programs after the creating a `wl_surface`, which means if we
want wl_surface for every widget, we have to allocate the `OpenGL` resources for
all the widgets while essentially we need just create a `weston_view` and
destroy the `weston_view` at the server. With this setup, It seems that I break
the tree structure of the `appsurface`. `shell_panel` will have two `appsurface`,
one for itself, one for the active widget, and the `find_subapp_at_xy` becomes
directly `find_widget_at_xy`. `n_subapp` becomes `n_widgets`, and at last,
`add_new_app` becomes `add_new_widget`. What represents the widgets really
becomes just active icons.

### buffer format
We are facing the problem of choosing buffer format for applications. Cairo
doesn't like OpenGL's RGBA, if we want to use cairo's image, we have to put the
image format into BGRA, as it follows that order.


##


## tw_output protocol
This protocol is create for the need of logical output representation. Server
side is finished. Client side needs more work.

First problem is that we do not know if `tw_output` is created or not before
`taiwins_shell` interface. Before we have `output_init` and `output_create`
methods, it is fairly ugly. Change them to a stack or queue implementation if
possible. List is not ideal since you will need to create additional struct.


We probably do not even need the shell interface anymore. Don't you need one for
sending events like `change_workspace`?

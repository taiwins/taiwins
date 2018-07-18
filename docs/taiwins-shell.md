# Taiwins-shell architecture

The `taiwins-shell` is the shell interface that interacts with the
`taiwins-server`. It implements the **arriere plane**, **widget and activitiy
panel** and **application launcher**. Maybe even the **verrou(casier)**. All the
functionalities are done dans une application, so it requires a delicate
structure to handle all the `events/requests`. Currently it consists two
threads, the main thread `wayland-dispatch-loop-processor`, mostly processes all
the wayland events(like input, buffer release, output created, etc.) and a
`client-side-event-processor`, we need it because there are also other events
which are not covered by a wayland proxy, for example, when you laptop battery
is dying, the shell need to know. When one minute passes, shell need to update
the panel.


# client-side-event-processor
This dude does two things, `poll` on the `inotify` file, and then timeout and
execute it(the execute I mean create a event for the main thread). The previous
design was to have a queue and let the main process to clean out the queue. But
we are stuck in the main process if there is nothing to dispatch.

From the triggering perspective, two different types events arrive in the queue,
the ones that triggered by system, `inotify event` and the ones triggered by
time as `timeout event`. From other perspective, for example, **local events**
and **proxy events**, the **local events** only concern inside the clients,
updating local buffer but no committing. **proxy events** will push all those
events and send it to the server.

| type           | A              | B              |
| ----           | ------         | ------         |
| executing type | local events   | proxy events   |
| arriving type  | inotify events | timeout events |

Unfortunately, right now we have to process them differently, `inotify event`
and `timeout event` can be distinguished by the return value from `poll`. But
`proxy event` and `local events` only different by its nature (I mean there is
no way you can distinguish it self). More likely, the event queue are.

	poll()
	for event in local_events:
		if event in inotify_events and event.triggered():
			event.do()
		else if event in timeout_events and event.timeout():
			event.do()
	for event in proxy_events:
		event.do()

Another problem arises that when one of the events cannot be processed
immediately, for example, the resource for the event to process is not
available, then in this case, the best that we can do is queuing the event and
try again later. Otherwise we will lose it. If this situation happens to local
event, in the beginning I feel like I need to re-processed before the all the
proxy-events, otherwise we need to wait another `poll`, but let's face it, the
resource won't be available for the between the interval of two `poll`s, so it
need to be queued for next interval. For `timeout event`, we might as well
discard it, but for `inotify event`, because there is no way to know when it
will happen again, we have to update it as soon as we hold the resource again.

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

This structure is now complex and prone to changes, we have to find a way to
re-design it.

# app-surface

This struct was designed for all the wayland client which requires a
`wl_surface`. Right now we have three different types of `app-surface`:
background, panel and widgets. It is double-buffered, has simple input
handlers. Now it also has children and the children occupies part of its
surface. This is not a very obvious design, since every `wl_surface` should be
independent, so how come it occupies other people's buffer. In the
`taiwins-shell` case, this is caused by panel has widgets hook on it and the
widgets has icons that occupy the panel, the widget itself also has a
`wl_surface` if you click on the icon.

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

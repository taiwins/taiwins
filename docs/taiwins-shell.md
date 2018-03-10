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


#### client-side-event-processor
This dude does two things, `poll` on the `inotify` file, and then timeout and
execute it(the execute I mean create a event for the main thread). The previous
design was to have a queue and let the main process to clean out the queue. But
we are stuck in the main process if there is nothing to dispatch. So it was
kinda awkward.

#### app-surface
This struct was designed for all the wayland client which requires a
`wl_surface`. It is double-buffered, has simple input handlers. Now it also has
children, the children occupies part of its surface. This is not a very obvious
design, since every `wl_surface` should be independent, so how come it occupies
other people's buffer. In the `taiwins-shell` case, this is caused by panel has
widgets hook on it and the widgets has icons that occupy the panel, the widget
itself also has a `wl_surface` if you click on the icon.

Actually this is necessary in a general the UI designs, when we are in parent UI
and want to access one of the children UI, we need an access point, like a
button, and this buffer can inhabit inside its parent or itself, since we
chooses the later, we need to let the parent know how to access it.

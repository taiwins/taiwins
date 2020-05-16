# Client data structures

-   **tw\_globals:** the global structure that fits the need of an wayland
    client.  It contains things like `wl_shm`, `wl_shm_format`, `wl_compositor`,
    `wl_display`.
    
    -   **Inputs:** We only have one input per client, this would be connect to
        one seat, input will handle

-   **tw\_event\_queue:** introduced originally for `tw_shell` to handle system
    generated events like time lapse and wayland protocol events. Very much like
    `wl_event_loop`, which is not available in clients.
    
    -   **tw\_event:** this leads to another interface for use to create its own
		event and watch the event. It didn't go as far as the `wl_protocols`
		where you create custom protocols and have handlers for them.

-   **tw_shm\_pool:** the allocator for shared buffers, binds to `wl_buffer`.

## UIs

-   **tw\_appsurf:** it binds to an `wl_surface` for different
    roles(protocols). The surface has it implementation of buffer and input
    events, it has the data but no actual methods to interact with
    it. `tw_appsurf` was first created for distinguishing background surface and
    panel surface. So in a sense, Right now it is used for any kind of single
    surface app. Note that:
    -   As an `tw_appsurf`, it has it's own `<x,y,w,h>` values.
    -   It also has input-events, to generate the a new frame.

-   `tw_appsurf` is designed to work with different proxies, `taiwins_ui`(this
    is taiwins specific, drawn in UI layer), `wl_shell_surface` or
    `xdg_shell_surface` should all work, it is an wrap around `wl_surface` and
    provides convenient methods for commiting surface and do a new frame.

-   `tw_appsurf_event_filter`. But you know not everyting relates to a surface
	is about frame, would it be able to update something without drawing a new
	frame? This is why we have an event filter, you can instal the filter to a
	surface, it runs before the `tw_appsurf.frame` and it can optionally skip
	the `frame`.
        
### Example of UI implementation
You can implement a `tw_appsurf` for your needs using `*_impl_app_surf`, there
are already predefined implementatin you can take advantage of.

-   **background:** background is picture reading and posting, it could be
    implemented as animation, by calling a frame.

-   **nuklear\_backend:** this GUI library is implemented with OpenGL or cairo,
    it implements a `tw_appsurf` like a sub class, then use it users can use
    simply `tw_appsurf` to operate.

-   **embeded\_surface:** This is one that complicates the implementation. It
    does not have its own data, but also need a frame. The frame is triggered by
    events.

### frame vs other event.

Right now `tw_appsurf` has a central `frame` callback to deal with all events,
check out `tw_event` structure for what is available, note that those are the
common events. You may implement your specific event through `tw_event_queue`.

If you do animations, in wayland, you need to call the `commit` plus a `frame`
in the next done event. `tw_appsurf` already has `tw_appsurf_request_frame` to
request a draw call. Optionally, when you call `tw_appsurf_frame`, you can
specify it is animated.


### NUKLEAR backend using Wayland 
We implement a cairo and EGL based nuklear renderer for `tw_appsurf`, user can
invoke `nk_egl(cairo)_impl_appsurf` to setup `tw_appsurf` to work with
`nuklear`. Then you would be able to use all the nuklear functions in the frame
callback.

#### NUKLEAR pipeline
The nuklear backend does all the heavy job of implementing all the ui forms that
we need for GUI code and you can implement different backend for it. Backend
implementation boils down to take draw command from `nk_command` and draw basic
geometry, text and image.

- functions like `nk_label`, `nk_row` translate into `nk_command` though
  functions like `nk_draw_text`, `nk_draw_image`.
  
- if you decide to write a CPU backend, then you have to draw based on those
  command, uses for example (Cairo), remember to use the double buffer(required
  for wayland).

- if you use GPU backend, there is an additional helper function called
  `nk_convert`, which converts the command into vertex buffer, element based on
  layout you specified.

#### New Font and image
  `nk_wl_*_backend` implements a resource management, to load font and image,
  call `nk_wl_load_image` and `nk_wl_new_font`


# name conversion
### **create** and  **destroy**
when using create destroy, par exemple, `wl_shm_pool_create` and
`wl_shm_pool_destroy`, we are getting a pointer then free the pointer in the
end.

The `struct` is usually not visible, declared in header, defined in source
code. Good example is `nk_egl_backend`, since there could be only one
implementation of it.

### **init** and **end**
In this case, user has the control over the memory, but the interface controls
the heap if any, after calling `end`, the struct should returns to the init
state.

### **init** and **release**
Same as above.



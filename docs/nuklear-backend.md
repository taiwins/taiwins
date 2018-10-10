# NUKLEAR backend using Wayland EGL
the `backend` is based on `glfw3` implementation in the `nuklear` project, it is
indeed straightforward, we don't need much too say, only difference is inserting
the new frames on the input.

## memory leak problem
- `nk_free(ctx)` is required to call to clean up context.
- `nk_buffer_free(ctx)` is required to free the command (4096 bytes).
- `nk_font_atlas_clear(altas)` is required to clean up the font.
- destroy the `egl_window`, `egl_context`, `wl_egl_window` and `egl_surface`.
  I still cannot get 100 percent leak free, there must be something I missed.


## NUKLEAR pipeline
The nuklear backend is really awesome, it does all the heavy job that we need
for GUI code and you can implement different backend for it. In my case, I wrote
the wayland-egl backend. It seems that we can also use traditional backend like
cairo to do the job, it is still very fast.

There are quite a few stages.

### The make widget functions:
- functions like `nk_label`, `nk_row` translate into `nk_command` though
  functions like `nk_draw_text`, `nk_draw_image`.

- if you decide to write a CPU backend, then you have to draw based on those
  command, uses for example (Cairo), remember you have the double buffer.

- if you use GPU backend, there is an additional helper function called
  `nk_convert`, which converts the command into vertex buffer, element based on
  layout you specified.

## measures
Now it requires 1ms for the rendering, I am not sure why, it should take 1ms
instead.

It seems that it takes at least 1ms to render the widget(maybe not really the
GPU time, may be the data uploading). In either case we need to come up with
methods to reduce the computation.

- using command buffer comparison.
- adding fps support, so we can skip directly those inputs in the
  timestamp(accumulate maybe).

## New Font
- nuklear does not like additional memory, so of cause you
  have to store the `nk_rune` yourself.

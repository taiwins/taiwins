# NUKLEAR backend using Wayland EGL
the `backend` is based on `glfw3` implementation in the `nuklear` project, it is
indeed straightforward, we don't need much too say, only difference is inserting
the new frames on the input.

## memory leak problem
- `nk_free(ctx)` is required to call to clean up context.
- `nk_buffer_free(ctx)` is required to free the command (4096 bytes).
- `nk_font_atlas_clear(altas)` is required to clean up the font.
- destroy the `egl_window`, `egl_context`, `wl_egl_window` and `egl_surface`.


## NUKLEAR pipeline
The nuklear backend is really awesome, it does all the heavy job that we need
for GUI code and you can implement different backend for it. In my case, I wrote
the wayland-egl backend. It seems that we can also use traditional backend like
cairo to do the job, it is still very fast.

## measures
It seems that it takes at least 1ms to render the widget(maybe not really the
gpu time, may be the data uploading). In either case we need to come up with
methods to reduce the computation.

- using command buffer comparision.
- adding fps support, so we can skip directly those inputs in the
  timestamp(accumnate maybe).

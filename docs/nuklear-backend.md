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

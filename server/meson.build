taiwins_cargs = [
  '-D_GNU_SOURCE',
  '-DWLR_USE_UNSTABLE'
]
if get_option('enable-profiler')
   taiwins_cargs += '-D_TW_ENABLE_PROFILING'
endif

####### dummy server
taiwins_src = [
  'main.c',
  'taiwins.c',
  'input.c',
  'layer_render.c',
  'binding/bindings.c',
  'backend/backend.c',
  'backend/backend_impl.c',
  'backend/backend_seat.c',
  'backend/backend_output.c',
  'renderer/renderer.c',
  'renderer/shaders.c',
  'renderer/texture.c',
  'renderer/layer_renderer.c',
  'desktop/shell.c',
  'desktop/shell_layer.c',
  'desktop/xdg.c',
  'desktop/xdg_grab.c',
  'desktop/workspace.c',
  'desktop/layout.c',
  'desktop/layout_floating.c',

  'objects/utils.c',
  'objects/seat.c',
  'objects/seat_keyboard.c',
  'objects/seat_pointer.c',
  'objects/seat_touch.c',
  'objects/dmabuf.c',
  'objects/compositor.c',
  'objects/surface.c',
  'objects/region.c',
  'objects/buffer.c',
  'objects/layers.c',
  'objects/logger.c',
  'objects/profiler.c',
  'objects/subprocess.c',
  'objects/data_device.c',
  'objects/data_source.c',
  'objects/data_offer.c',
  'objects/data_dnd.c',
  'objects/matrix.c',
  'objects/plane.c',
  'objects/cursor.c',
  'objects/desktop.c',
  'objects/popup_grab.c',
  'objects/desktop_wl_shell.c',
  'objects/desktop_xdg_shell.c',

  wayland_linux_dmabuf_server_protocol_h,
  wayland_linux_dmabuf_private_code_c,
  wayland_viewporter_server_protocol_h,
  wayland_viewporter_private_code_c,
  wayland_presentation_time_server_protocol_h,
  wayland_presentation_time_private_code_c,
  wayland_xdg_shell_server_protocol_h,
  wayland_xdg_shell_private_code_c,
  wayland_xdg_output_server_protocol_h,
  wayland_xdg_output_private_code_c,

  wayland_taiwins_shell_server_protocol_h,
  wayland_taiwins_shell_private_code_c,
  wayland_wlr_layer_shell_server_protocol_h,
  wayland_wlr_layer_shell_private_code_c,
]
taiwins = executable(
  'taiwins',
  taiwins_src,
  c_args : taiwins_cargs,
  dependencies : [
    dep_wayland_server,
    dep_ctypes,
    dep_wlroots,
    dep_libweston, #finally we would get rid of this
    dep_xkbcommon,
    dep_threads,
    dep_pixman,
    dep_glesv2,
    dep_dl,
  ],
  include_directories : [
    inc_shared_config,
    include_directories('.'),
  ],
  install : true,
)

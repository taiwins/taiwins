####### dummy server
taiwins_src = [
  'main.c',
  'taiwins.c',
  'input.c',
  'backend/backend.c',
  'backend/keyboard.c',
  'backend/pointer.c',
  'backend/touch.c',
  'backend/render/renderer.c',
  'backend/render/shaders.c',
  'seat/seat.c',
  'seat/seat_keyboard.c',
  'seat/seat_pointer.c',
  'seat/seat_touch.c',
  'binding/bindings.c',

  'objects/dmabuf.c',
  'objects/compositor.c',
  'objects/surface.c',
  'objects/region.c',
  'objects/buffer.c',

  wayland_linux_dmabuf_server_protocol_h,
  wayland_linux_dmabuf_private_code_c,
  wayland_viewporter_server_protocol_h,
  wayland_viewporter_private_code_c,
  wayland_presentation_time_server_protocol_h,
  wayland_presentation_time_private_code_c,
  wayland_xdg_shell_server_protocol_h,
  wayland_xdg_shell_private_code_c,
]
taiwins = executable(
  'taiwins',
  taiwins_src,
  c_args : [
    '-D_GNU_SOURCE',
    '-DWLR_USE_UNSTABLE',
  ],
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

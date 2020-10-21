taiwins_lib_src += files(
  'backend.c',
  'seat.c',
  'protocol.c',
  'output.c',
)
taiwins_lib_src += [
  wayland_xdg_shell_client_protocol_h,
  wayland_presentation_time_client_protocol_h,
  wayland_presentation_time_private_code_c,
]

taiwins_lib_dep += [
  dep_wayland_client,
  dep_wayland_egl
]

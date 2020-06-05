####### dummy server
taiwins_src = [
  'main.c',
  'taiwins.c',
  'input.c',
  'backend/backend.c',
  'backend/keyboard.c',
  'backend/pointer.c',
  'backend/touch.c',
  'seat/seat.c',
  'seat/seat_keyboard.c',
  'seat/seat_pointer.c',
  'seat/seat_touch.c',
  'binding/bindings.c',
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
    dep_dl,
  ],
  include_directories : [
    inc_shared_config,
    include_directories('.'),
  ],
  install : true,
)

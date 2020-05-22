####### dummy server
taiwins_src = [
  'main.c'
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
    dep_threads,
    dep_pixman,
  ],
  include_directories : [
    inc_shared_config,
  ],
  install : true,
)

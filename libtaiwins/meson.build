version = '"@0@"'.format(meson.project_version())

taiwins_cargs = [
  '-D_GNU_SOURCE',
  '-D_TW_VERSION=@0@'.format(version),
]

subdir('objects')

pkgconfig = import('pkgconfig')

####### static lib
taiwins_lib_src = [
  'input_device.c',
  'output_device.c',
  'dbus_utils.c',

  'shell/shell.c',
  'shell/shell_layer.c',
  'shell/console.c',
  'shell/theme.c',

  'engine/engine.c',
  'engine/seat.c',
  'engine/output.c',

  wayland_taiwins_shell_server_protocol_h,
  wayland_taiwins_shell_private_code_c,
  wayland_taiwins_console_server_protocol_h,
  wayland_taiwins_console_private_code_c,
  wayland_taiwins_theme_server_protocol_h,
  wayland_taiwins_theme_private_code_c,
  wayland_wlr_layer_shell_server_protocol_h,
  wayland_wlr_layer_shell_private_code_c,
  wayland_xdg_shell_private_code_c,
]

taiwins_lib_dep = [
  dep_wayland_server,
  dep_twobjects,
  dep_ctypes,
  dep_xkbcommon,
  dep_threads,
  dep_pixman,
  dep_glesv2,
  dep_egl,
  dep_dl,
  dep_lua,
  dep_twclient_theme,
  dep_tdbus,
  dep_libinput
]

subdir('render')
subdir('libinput')
subdir('backend')
subdir('xwayland')

# taiwins_lib_src += option_h

taiwins_lib = both_libraries(
  'taiwins',
  taiwins_lib_src,
  c_args : taiwins_cargs,
  dependencies : taiwins_lib_dep,
  include_directories : [
    inc_shared_config,
    inc_libtaiwins,
  ],
  version: meson.project_version(),
  install: true,
)

dep_taiwins_lib = declare_dependency(
  link_with : taiwins_lib,
  compile_args : taiwins_cargs,
  dependencies : taiwins_lib_dep,
  include_directories : [
    inc_shared_config,
    inc_libtaiwins,
  ],
)

version = '"@0@"'.format(meson.project_version())

taiwins_cargs = [
  '-D_GNU_SOURCE',
  '-DWLR_USE_UNSTABLE',
  '-D_TW_VERSION=@0@'.format(version),
]

if get_option('enable-profiler')
  taiwins_cargs += '-D_TW_ENABLE_PROFILING'
endif

if get_option('rendering-debug') == 'damage'
  taiwins_cargs += '-D_TW_DEBUG_DAMAGE'
elif get_option('rendering-debug') == 'clip'
  taiwins_cargs += '-D_TW_DEBUG_CLIP'
endif

pkgconfig = import('pkgconfig')

####### static lib
taiwins_lib_src = [
  'bindings.c',
  'input_device.c',
  'output_device.c',

  'shell/shell.c',
  'shell/shell_layer.c',
  'shell/console.c',
  'shell/theme.c',
  'desktop/xdg.c',
  'desktop/xdg_grab.c',
  'desktop/workspace.c',
  'desktop/layout.c',
  'desktop/layout_floating.c',
  'desktop/layout_maximized.c',
  'desktop/layout_fullscreen.c',
  'desktop/layout_tiling.c',

  'render/render_context.c',
  'render/render_output.c',
  'render/render_pipeline.c',
  'render/egl_render_context.c',
  'render/egl_texture.c',
  'render/egl_shaders.c',
  'render/egl_simple_pipeline.c',

  'engine/engine.c',
  'engine/seat.c',
  'engine/output.c',

  'config/config.c',
  'config/config_bindings.c',
  'config/config_parser.c',
  'config/config_lua.c',
  'config/config_bus.c',
  'config/theme_lua.c',

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

subdir('backend')

taiwins_lib = static_library(
  'taiwins',
  taiwins_lib_src,
  c_args : taiwins_cargs,
  dependencies : taiwins_lib_dep,
  include_directories : [
    inc_shared_config,
    inc_libtaiwins,
  ],
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

####### compositor
taiwins_src = [
  'compositor/main.c',
  'compositor/input.c',
  wayland_taiwins_shell_server_protocol_h,
]

taiwins = executable(
  'taiwins',
  taiwins_src,
  c_args : taiwins_cargs,
  dependencies : dep_taiwins_lib,
  install : true,
)

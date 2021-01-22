taiwins_cargs = []

if get_option('rendering-debug') == 'damage'
  taiwins_cargs += '-D_TW_DEBUG_DAMAGE'
elif get_option('rendering-debug') == 'clip'
  taiwins_cargs += '-D_TW_DEBUG_CLIP'
endif

if get_option('xwayland').enabled()
  taiwins_cargs += '-D_TW_HAS_XWAYLAND'
endif


taiwins_src = [
  'main.c',
  'input.c',
  'bindings.c',
  'egl_renderer.c',

  'desktop/xdg.c',
  'desktop/xdg_grab.c',
  'desktop/workspace.c',
  'desktop/layout.c',
  'desktop/layout_floating.c',
  'desktop/layout_maximized.c',
  'desktop/layout_fullscreen.c',
  'desktop/layout_tiling.c',

  'config/config.c',
  'config/config_bindings.c',
  'config/config_parser.c',
  'config/config_lua.c',
  'config/config_bus.c',
  'config/theme_lua.c',

  wayland_taiwins_shell_server_protocol_h,
]

taiwins = executable(
  'taiwins',
  taiwins_src,
  c_args : taiwins_cargs,
  dependencies : dep_taiwins_lib,
  include_directories : include_directories('.'),
  install : true,
)

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

if cc.has_function('libinput_device_touch_get_touch_count',
		   prefix: '#include <libinput.h>',
		  dependencies: [dep_libinput])
  taiwins_cargs += '-D_TW_HAS_LIBINPUT_GET_TOUCH_COUNT'
endif

pkgconfig = import('pkgconfig')

####### server
taiwins_src = [
  'main.c',
  'input.c',
  'bindings.c',
  'backend/backend.c',
  'backend/backend_impl.c',
  'backend/backend_seat.c',
  'backend/backend_libinput.c',
  'backend/backend_output.c',
  'renderer/renderer.c',
  'renderer/shaders.c',
  'renderer/texture.c',
  'renderer/layer_renderer.c',
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

taiwins = executable(
  'taiwins',
  taiwins_src,
  c_args : taiwins_cargs,
  dependencies : [
    dep_wayland_server,
    dep_twobjects,
    dep_ctypes,
    dep_wlroots,
    dep_xkbcommon,
    dep_threads,
    dep_pixman,
    dep_glesv2,
    dep_egl,
    dep_dl,
    dep_lua,
    dep_twclient_theme,
    dep_tdbus,
    dep_libinput,
  ],
  include_directories : [
    inc_shared_config,
    include_directories('.'),
    include_directories('include'),
  ],
  install : true,
)

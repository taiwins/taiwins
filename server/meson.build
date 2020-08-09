taiwins_cargs = [
  '-D_GNU_SOURCE',
  '-DWLR_USE_UNSTABLE'
]

if get_option('enable-profiler')
   taiwins_cargs += '-D_TW_ENABLE_PROFILING'
endif

if cc.has_function('libinput_device_touch_get_touch_count',
		   prefix: '#include <libinput.h>',
		  dependencies: [dep_libinput])
  taiwins_cargs += '-D_TW_HAS_LIBINPUT_GET_TOUCH_COUNT'
endif

pkgconfig = import('pkgconfig')

subdir('include')

####### taiwins objects

taiwins_obj_srcs = [
  'objects/utils.c',
  'objects/seat/seat.c',
  'objects/seat/seat_keyboard.c',
  'objects/seat/seat_pointer.c',
  'objects/seat/seat_touch.c',
  'objects/dmabuf.c',
  'objects/compositor.c',
  'objects/surface.c',
  'objects/region.c',
  'objects/buffer.c',
  'objects/layers.c',
  'objects/logger.c',
  'objects/profiler.c',
  'objects/subprocess.c',
  'objects/data_device/data_device.c',
  'objects/data_device/data_source.c',
  'objects/data_device/data_offer.c',
  'objects/data_device/data_dnd.c',
  'objects/matrix.c',
  'objects/plane.c',
  'objects/cursor.c',
  'objects/popup_grab.c',
  'objects/desktop/desktop.c',
  'objects/desktop/desktop_wl_shell.c',
  'objects/desktop/desktop_xdg_shell.c',

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
]

taiwins_objects_inc = include_directories('include')

taiwins_objects_deps = [
    dep_wayland_server,
    dep_ctypes,
    dep_pixman,
    dep_xkbcommon,
    dep_glesv2,
    dep_libdrm,
    dep_dl,
    dep_m,
]

lib_taiwins_objects = both_libraries(
  'taiwins-objects',
  taiwins_obj_srcs,
  c_args : taiwins_cargs,
  dependencies : taiwins_objects_deps,
  include_directories : taiwins_objects_inc,
  install : true,
  )

dep_taiwins_objects = declare_dependency(
  link_with : lib_taiwins_objects,
  compile_args : taiwins_cargs,
  include_directories : taiwins_objects_inc,
  dependencies : taiwins_objects_deps,
)

pkgconfig.generate(
  lib_taiwins_objects,
  filebase : 'taiwins-objects',
  name : 'taiwins objects',
  version: meson.project_version(),
  description: 'Taiwins wayland objects library',
)

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
    dep_taiwins_objects,
    dep_ctypes,
    dep_wlroots,
    dep_xkbcommon,
    dep_threads,
    dep_pixman,
    dep_glesv2,
    dep_dl,
    dep_lua,
    dep_twclient_theme,
    dep_tdbus,
  ],
  include_directories : [
    inc_shared_config,
    include_directories('.'),
  ],
  install : true,
)

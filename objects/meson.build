twobject_cargs = [
  '-D_GNU_SOURCE'
]

foreach f : [ 'mkostemp', 'posix_fallocate', 'memfd_create' ]
  farg = cc.has_function(f) ? ['-DHAVE_' + f.underscorify().to_upper()] : []
  twobject_cargs += farg
endforeach

if cc.has_header('EGL/eglmesaext.h')
  twobject_cargs += '-DHAVE_EGLMESAEXT'
endif

pkgconfig = import('pkgconfig')

taiwins_obj_srcs = [
  'utils.c',
  'seat/seat.c',
  'seat/seat_keyboard.c',
  'seat/seat_pointer.c',
  'seat/seat_touch.c',
  'serial_engine.c',
  'dmabuf.c',
  'output.c',
  'compositor.c',
  'surface.c',
  'subsurface.c',
  'region.c',
  'buffer.c',
  'layers.c',
  'logger.c',
  'profiler.c',
  'subprocess.c',
  'data_device/data_device.c',
  'data_device/data_source.c',
  'data_device/data_offer.c',
  'data_device/data_dnd.c',
  'matrix.c',
  'plane.c',
  'cursor.c',
  'popup_grab.c',
  'desktop/desktop.c',
  'desktop/desktop_wl_shell.c',
  'desktop/desktop_xdg_shell.c',
  'presentation_feedback.c',
  'viewporter.c',
  'os-compatibility.c',
  'drm_formats.c',
  'egl.c',

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

twobjects_deps = [
    dep_wayland_server,
    dep_pixman,
    dep_xkbcommon,
    dep_libdrm,
    dep_m,
    dep_egl,
    dep_glesv2,
]

lib_twobjects = both_libraries(
  'taiwins-objects',
  taiwins_obj_srcs,
  c_args : twobject_cargs,
  dependencies : twobjects_deps,
  include_directories : twobjects_inc,
  version: meson.project_version(),
  install : true,
  )

dep_twobjects = declare_dependency(
  link_with : lib_twobjects,
  compile_args : twobject_cargs,
  include_directories : twobjects_inc,
  dependencies : twobjects_deps,
)

pkgconfig.generate(
  lib_twobjects,
  filebase : 'twobjects',
  name : 'taiwins objects',
  version: meson.project_version(),
  description: 'Taiwins wayland objects library',
)

debug_cargs = ['-D_GNU_SOURCE']
if get_option('rendering-debug') == 'damage'
  debug_cargs += '-D_TW_DEBUG_DAMAGE'
elif get_option('rendering-debug') == 'clip'
  debug_cargs += '-D_TW_DEBUG_CLIP'
endif


lib_fakedlclose=shared_library(
  'fakedlclose',
  ['fakedlclose.c'],
  install : false,
)

dummy_surface_test = executable(
  'tw-test-dummy-surface',
  ['dummy-surface-test.c'],
  c_args : debug_cargs,
  dependencies : [
    dep_wayland_client,
    dep_twclient,
  ],
  install : false,
)

dummy_surface_test_gl = executable(
  'tw-test-dummy-surface-gl',
  [
    'dummy-surface-test-gl.c',
    wayland_xdg_shell_client_protocol_h,
    wayland_xdg_shell_private_code_c,
  ],
  c_args : debug_cargs,
  dependencies : [
    dep_wayland_client,
    dep_twclient,
  ],
  install : false,
)

matrix_test = executable(
  'tw-test-matrix',
  ['matrix-test.c'],
  c_args : ['-D_GNU_SOURCE'],
  dependencies : [
    dep_m,
    dep_twobjects,
  ],
)
test('test_matrix_test', matrix_test)

egl_test_context = executable(
  'tw-test-egl-context',
  'egl-context-test.c',
  c_args : debug_cargs,
  dependencies : dep_twobjects,
)
test('test_egl_context', egl_test_context)

headless_test = executable(
  'tw-test-headless',
  'headless-test.c',
  c_args : ['-D_GNU_SOURCE', '-DWLR_USE_UNSTABLE'],
  dependencies : dep_taiwins_lib,
)
test('test_headless', headless_test)

if get_option('x11-backend').enabled()
  x11_test = executable(
    'tw-test-x11',
    ['x11-test.c', '../compositor/egl_renderer.c', 'test_desktop.c',
     wayland_taiwins_shell_server_protocol_h],
    c_args : debug_cargs,
    dependencies : dep_taiwins_lib,
  )
  test('test_x11', x11_test)
endif


wayland_test = executable(
  'tw-test-wayland',
  ['wayland-test.c', '../compositor/egl_renderer.c', 'test_desktop.c',
   wayland_taiwins_shell_server_protocol_h],
  c_args : debug_cargs,
  dependencies : dep_taiwins_lib,
)
test('test_wayland', wayland_test)

drm_test = executable(
  'tw-test-drm',
  ['drm-test.c', '../compositor/egl_renderer.c', 'test_desktop.c' ],
  c_args : debug_cargs,
  dependencies : dep_taiwins_lib,
)

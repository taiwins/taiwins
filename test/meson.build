lib_fakedlclose=shared_library(
  'fakedlclose',
  ['fakedlclose.c'],
  install : false,
)

dummy_surface_test = executable(
  'tw-test-dummy-surface',
  ['dummy-surface-test.c'],
  c_args : ['-D_GNU_SOURCE'],
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
  c_args : ['-D_GNU_SOURCE'],
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
  ['egl-context-test.c',
  '../server/renderer/egl.c'],
  c_args : ['-D_GNU_SOURCE'],
  dependencies : [
    dep_m,
    dep_twobjects,
    dep_glesv2,
    dep_egl,
  ],
  include_directories : [
    include_directories('../server/include'),
  ],
)
test('test_egl_context', egl_test_context)

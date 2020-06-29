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
  ['dummy-surface-test-gl.c'],
  c_args : ['-D_GNU_SOURCE'],
  dependencies : [
    dep_wayland_client,
    dep_twclient,
  ],
  install : false,
)

matrix_test = executable(
  'tw-test-matrix',
  ['matrix-test.c',
   '../server/objects/matrix.c'],
  c_args : ['-D_GNU_SOURCE'],
  include_directories : [ '../server/objects'],
  dependencies : [
    dep_m,
  ],
)
test('test_matrix_test', matrix_test)

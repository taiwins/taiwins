x11_required = [
  'x11-xcb',
  'xcb',
  'xcb-xinput',
  'xcb-xfixes',
]
deps_x11 = []

if get_option('x11-backend').enabled()
  options_data.set10('_TW_HAS_X11_BACKEND', true)
  msg = ['@0@ not found for x11-backend']

  foreach lib : x11_required
    dep = dependency(lib, required: true,
		     not_found_message: '\n'.join(msg).format(lib))
    if not dep.found()
      subdir_done()
    endif
    deps_x11 += dep
  endforeach

  taiwins_lib_src += files(
    'backend.c',
    'input.c',
    'output.c'
  )
  taiwins_lib_dep += deps_x11
endif

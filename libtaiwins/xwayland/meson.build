xwayland_required = [
	'xcb',
	'xcb-composite',
	'xcb-render',
	'xcb-xfixes',
]
deps_xwayland = []

if get_option('xwayland').enabled()
  options_data.set10('_TW_HAS_XWAYLAND', true)
  msg = ['@0@ not found for xwayland']

  foreach lib : xwayland_required
    dep = dependency(lib, required: true,
		     not_found_message: '\n'.join(msg).format(lib))
    if not dep.found()
      subdir_done()
    endif
    deps_xwayland += dep
  endforeach

  taiwins_lib_src += files('server.c')
  taiwins_lib_dep += deps_xwayland
endif

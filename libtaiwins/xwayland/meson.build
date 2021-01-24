xwayland_required = [
  'xcb',
  'xcb-composite',
  'xcb-render',
  'xcb-xfixes',
]

xwayland_required_optional = [
  'xcb-icccm',
  'xcb-errors',
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

  foreach lib : xwayland_required_optional
    dep = dependency(lib, required: false,
		    not_found_message: '\n'.join(msg).format(lib))
    if dep.found()
      deps_xwayland += dep
      options_data.set10('_TW_HAS_' + lib.underscorify().to_upper(), true)
    endif
  endforeach

  taiwins_lib_src += files('server.c', 'xwm.c', 'window.c', 'atoms.c', 'selection.c')
  taiwins_lib_dep += deps_xwayland
endif

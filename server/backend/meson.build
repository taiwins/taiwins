### for libinput backend
if cc.has_function('libinput_device_touch_get_touch_count',
		   prefix: '#include <libinput.h>',
		  dependencies: [dep_libinput])
  taiwins_cargs += '-D_TW_HAS_LIBINPUT_GET_TOUCH_COUNT'
endif

taiwins_lib_src += files(
  'backend.c',
  'libinput.c',
  'objects_proxy.c',
  'output.c',
  'seat.c',
  'headless.c',
)

##### x11 backend

x11_required = [
  'x11-xcb',
  'xcb',
  'xcb-xinput',
  'xcb-xfixes',
]
deps_x11 = []

if get_option('x11-backend').enabled()
  msg = ['@0@ not found for x11-backend']
  foreach lib : x11_required
    dep = dependency(lib, required: true,
		     not_found_message: '\n'.join(msg).format(lib))
    if not dep.found()
      subdir_done()
    endif
    deps_x11 += dep
  endforeach
endif

taiwins_lib_dep += deps_x11

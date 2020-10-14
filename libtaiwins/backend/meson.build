### for libinput backend
if cc.has_function('libinput_device_touch_get_touch_count',
		   prefix: '#include <libinput.h>',
		  dependencies: [dep_libinput])
  taiwins_cargs += '-D_TW_HAS_LIBINPUT_GET_TOUCH_COUNT'
endif

taiwins_lib_src += files(
  'headless.c',
)

subdir('x11')
subdir('wayland')

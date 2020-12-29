### for libinput backend
if cc.has_function('libinput_device_touch_get_touch_count',
		   prefix: '#include <libinput.h>',
		   dependencies: [dep_libinput])
  options_data.set10('_TW_HAS_LIBINPUT_GET_TOUCH_COUNT', true)
endif

taiwins_lib_src += files(
  'backend.c',
  'headless.c',
)

subdir('x11')
subdir('wayland')
subdir('drm')
subdir('libinput')

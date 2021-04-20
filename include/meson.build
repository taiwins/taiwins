exclude_files = ['meson.build', 'profiling.h', 'dbus_utils.h']

if get_option('profiler').enabled()
  options_data.set10('_TW_ENABLE_PROFILING', true)
endif

if not get_option('x11-backend').enabled()
  exclude_files += 'backend-x11.h'
endif

if not get_option('xwayland').enabled()
  exclude_files += 'xwayland.h'
endif

install_subdir('taiwins',
	install_dir: get_option('includedir'),
	exclude_files: exclude_files,
)

####### options.h

options_data.set10('_TW_HAS_EGLMESAEXT', cc.has_header('EGL/eglmesaext.h'))
if cc.has_function('libinput_device_touch_get_touch_count',
		   prefix: '#include <libinput.h>',
		   dependencies: [dep_libinput])
  options_data.set10('_TW_HAS_LIBINPUT_GET_TOUCH_COUNT', true)
else
  options_data.set10('_TW_HAS_LIBINPUT_GET_TOUCH_COUNT', false)
endif

option_h = configure_file(
  input: 'options.h.in',
  output: 'options.h',
  configuration: options_data,
)

exclude_files = ['meson.build', 'profiling.h']

if get_option('enable-profiler')
  options_data.set10('_TW_ENABLE_PROFILING', true)
endif

if not get_option('x11-backend').enabled()
	exclude_files += 'backend-x11.h'
endif

install_subdir('taiwins',
	install_dir: get_option('includedir'),
	exclude_files: exclude_files,
)

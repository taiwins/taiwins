exclude_files = ['meson.build', 'profiling.h']
if not get_option('x11-backend').enabled()
	exclude_files += 'taiwins/backend/x11.h'
endif

install_subdir('taiwins',
	install_dir: get_option('includedir'),
	exclude_files: exclude_files,
)

project('twobjects', 'c',
	version : '0.1.1',
	default_options: [
	  'warning_level=2', #warn level has to be 2 to get wlroots compiles
	  'c_std=gnu11',
	  'werror=true',
	],
	meson_version: '>= 0.46',
	license: 'GPLv2+')

###### compile flags
cc = meson.get_compiler('c')

global_args_maybe = [
	'-Wno-unused-parameter',
	'-Wno-missing-field-initializers',
	'-Wno-pedantic',
]
arg_visibility = get_option('buildtype') != 'debug' ? ['-fvisibility=hidden'] : []
global_args_maybe += arg_visibility

add_project_arguments(
  cc.get_supported_arguments(global_args_maybe),
  language : 'c'
)

dep_pixman = dependency('pixman-1', version: '>= 0.25.2')
dep_wayland_server = dependency('wayland-server', version: '>= 1.12.0')
dep_xkbcommon = dependency('xkbcommon', version: '>= 0.3.0')
dep_libdrm = dependency('libdrm', version: '>= 2.4.68')
dep_m = cc.find_library('m')
dep_glesv2 = dependency('glesv2')
dep_egl = dependency('egl')

twobjects_inc = include_directories('include')

subdir('include')
subdir('protocols')
subdir('objects')

project('taiwins', 'c',
	version : '0.1.1',
	default_options: [
	  'warning_level=3',
	  'c_std=gnu11',
	  'werror=true'
	],
	meson_version: '>= 0.47',
	license: 'GPLv2+')

### compile flags
cc = meson.get_compiler('c')

global_args_maybe = [
	'-Wno-unused-parameter',
	'-Wno-missing-field-initializers',
	'-Wno-pedantic',

]
global_args = []

foreach a : global_args_maybe
  if cc.has_argument(a)
    global_args += a
  endif
endforeach

if get_option('buildtype') == 'debug' and cc.has_argument('-fsanitize=address')
  global_args += '-fsanitize=address' #this options hides most of static functions
# elif get_option('buildtype') == 'release' and cc.has_argument('-fvisibility')
#   global_args += '-fvisibility' #this options hides most of static functions
endif


##### find packages
dep_pixman = dependency('pixman-1', version: '>= 0.25.2')
dep_xkbcommon = dependency('xkbcommon', version: '>= 0.3.0')
dep_wayland_server = dependency('wayland-server', version: '>= 1.12.0')
dep_wayland_client = dependency('wayland-client', version: '>= 1.12.0')
dep_threads = dependency('threads')
dep_pam = cc.find_library('pam')

subproject('twclient')
subproject('nklua')
subproject('tdbus')
subproject('libweston',
	   default_options: [
	     'backend-rdp=false',
	     'simple-dmabuf-drm=auto',
	   ])

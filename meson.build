project('taiwins', 'c',
	version : '0.1.1',
	default_options: [
	  'warning_level=2', #warn level has to be 2 to get wlroots compiles
	  'c_std=gnu11',
	  'werror=true',
	],
	meson_version: '>= 0.5',
	license: 'GPLv2+')


###### compile flags
cc = meson.get_compiler('c')

global_args = []
global_link_args = []
global_args_maybe = [
	'-Wno-unused-parameter',
	'-Wno-missing-field-initializers',
	'-Wno-pedantic',
]

foreach a : global_args_maybe
  if cc.has_argument(a)
    global_args += a
  endif
endforeach

if get_option('buildtype') == 'debug' and get_option('sanitizer') and cc.has_argument('-fsanitize=address')
  global_args += '-fsanitize=address'
  global_link_args += '-fsanitize=address'
endif

add_project_arguments(global_args, language : 'c')
add_project_link_arguments(global_link_args, language : 'c')

###### subprojects
wlroots_options = [
  'c_std=c11',
  'warning_level=2',
  'werror=true',
  'examples=false',
]

twclient = subproject('twclient')
nklua = subproject('nklua')
tdbus = subproject('tdbus')
ctypes = subproject('ctypes')
wlroots = subproject('wlroots', default_options : wlroots_options )

###### dependencies
dep_pixman = dependency('pixman-1', version: '>= 0.25.2')
dep_xkbcommon = dependency('xkbcommon', version: '>= 0.3.0')
dep_wayland_server = dependency('wayland-server', version: '>= 1.12.0')
dep_wayland_client = dependency('wayland-client', version: '>= 1.12.0')
dep_lua = dependency('lua', version: '>= 5.3', required: true)
dep_threads = dependency('threads')
dep_libweston = dependency('libweston-8') #TODO: finally we would get rid of libweston
dep_libweston_desktop = dependency('libweston-8') #TODO: finally we would get rid of libweston
dep_glesv2 = dependency('glesv2')
dep_wlroots = wlroots.get_variable('wlroots')
dep_pam = cc.find_library('pam')
dep_dl = cc.find_library('dl')
dep_ctypes = ctypes.get_variable('dep_ctypes')
dep_twclient = twclient.get_variable('dep_twclient')
dep_twclient_theme = twclient.get_variable('dep_twclient_theme')
dep_twclient_icons = twclient.get_variable('dep_twclient_icons')
dep_twclient_nkcairo = twclient.get_variable('dep_nkcairo')
dep_nklua = nklua.get_variable('dep_nklua')
dep_tdbus = tdbus.get_variable('dep_tdbus')

inc_shared_config = include_directories('.')
dir_tp_base = join_paths(meson.source_root(), 'subprojects', 'taiwins-protocols')

subdir('protocols')
subdir('server')
subdir('clients')

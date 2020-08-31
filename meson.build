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

add_project_arguments(global_args, language : 'c')
add_project_link_arguments(global_link_args, language : 'c')

###### subprojects
twclient = subproject('twclient')
nklua = subproject('nklua')
tdbus = subproject('tdbus')
ctypes = subproject('ctypes')
wlroots = subproject('wlroots', default_options : [
		       'c_std=c11',
		       'warning_level=2',
		       'werror=true',
		       'examples=false',
		     ])
pixman = subproject('pixman', default_options: ['gtk=disabled', 'libpng=disabled','werror=false'],
		    version: '>= 0.25.2', required: false)

###### dependencies
if pixman.found()
  dep_pixman = pixman.get_variable('idep_pixman')
else
  dep_pixman = dependency('pixman-1', version: '>= 0.25.2')
endif

if wlroots.found()
  dep_wlroots = wlroots.get_variable('wlroots')
else
  dep_wlroots = dependency('wlroots', required: true)
endif

dep_lua = dependency('lua', version: '>= 5.3', required: false)
if not dep_lua.found()
  dep_lua = dependency('lua-5.3', version: '>= 5.3', required: true)
endif

dep_xkbcommon = dependency('xkbcommon', version: '>= 0.3.0')
dep_wayland_server = dependency('wayland-server', version: '>= 1.12.0')
dep_wayland_client = dependency('wayland-client', version: '>= 1.12.0')
dep_threads = dependency('threads')
dep_glesv2 = dependency('glesv2')
dep_libdrm = dependency('libdrm', version: '>= 2.4.68')
dep_libinput = dependency('libinput', version: '>=1.9.0')
dep_pam = cc.find_library('pam')
dep_m = cc.find_library('m')
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
subdir('test')


#docs
if get_option('build-doc')
  doxygen = find_program('doxygen', required: true)
  docdata = configuration_data()
  docdata.set('VERSION', meson.project_version())

  if find_program('dot', required : false).found()
    docdata.set('HAVE_DOT', 'YES')
  else
    docdata.set('HAVE_DOT', 'NO')
  endif
  subdir('docs')
endif

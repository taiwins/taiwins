dep_scanner = dependency('wayland-scanner', native: true)
dep_wp = dependency('wayland-protocols', version: '>= 1.18')
prog_scanner = find_program(dep_scanner.get_pkgconfig_variable('wayland_scanner'))
dir_wp_base = dep_wp.get_pkgconfig_variable('pkgdatadir')
protocols = [['taiwins-shell', 'taiwins'],
	     ['taiwins-console', 'taiwins'],
	     ['taiwins-theme', 'taiwins'],
	     ['linux-dmabuf', 'v1']]

foreach proto : protocols
  if proto[1] == 'taiwins'
    base_file = proto[0]
    xml_path = '@0@/@1@.xml'.format(dir_tp_base, proto[0])
  elif proto[1] == 'internal'
    base_file = proto[0]
    xml_path = '@0@/@1@.xml'.format(meson.current_source_dir(), proto[1])
  elif proto[1] == 'stable'
    base_file = proto[0]
    xml_path = '@0@/@1@/@2@/@2@.xml'.format(dir_wp_base, proto[1], proto[0])
  else #unstable
    base_file = '@0@-unstable-@1@'.format(proto[0], proto[1])
    xml_path = '@0@/unstable/@1@/@2@.xml'.format(dir_wp_base, proto[0], base_file)
  endif
  base_file = proto[0]
  foreach type : [ 'client-header', 'server-header', 'private-code']
    if type == 'client-header'
      output_file = 'wayland-@0@-client-protocol.h'.format(base_file)
    elif type == 'server-header'
      output_file = 'wayland-@0@-server-protocol.h'.format(base_file)
    else
      output_file = 'wayland-@0@-private-code.c'.format(base_file)
    endif
    target = custom_target(
      '@0@-@1@'.format(base_file, type),
      command : [ prog_scanner, type, '@INPUT@', '@OUTPUT@'],
      input : files(xml_path),
      output : output_file,
    )
    var_name = output_file.underscorify()
    set_variable(var_name, target)
  endforeach
endforeach

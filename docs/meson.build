docdata.set('TOP_SRCDIR', meson.source_root())
docdata.set('TOP_BUILDIR', meson.build_root())
docdata.set('PROJECT_NAME', 'Taiwins')
docdata.set('SUBPROJ_DIR', join_paths(meson.source_root(), 'subprojects'))

doxyfile = configure_file(input: 'Doxyfile.in',
                          output: 'Doxyfile',
                          configuration: docdata,
                          install: false)

datadir = join_paths(get_option('datadir'), 'doc', 'taiwins')

html_target = custom_target('doc',
			    input: doxyfile,
			    output: 'docs',
			    command: [doxygen, doxyfile],
                            install: true,
                            install_dir: datadir)

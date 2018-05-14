cdata = configuration_data()
cdata.set('SRCDIR', join_paths(meson.source_root(), 'libfprint'))
cdata.set('BUILDDIR', join_paths(meson.build_root(), 'doc'))

cfg = configure_file(input: 'doxygen.cfg.in',
                     output: 'doxygen.cfg',
                     configuration: cdata,
                     install: false)

doxygen = find_program('doxygen')
datadir = join_paths(get_option('datadir'), 'doc', 'libfprint-devel')
custom_target('docs',
              input: cfg,
              output: 'html',
              command: [ doxygen, '@INPUT@' ],
              install: true,
              install_dir: datadir)

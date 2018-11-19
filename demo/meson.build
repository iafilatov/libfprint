gtk_test_resources = gnome.compile_resources('gtk-test-resources', 'gtk-libfprint-test.gresource.xml',
                                             source_dir : '.',
                                             c_name : 'gtk_test')

prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))

executable('gtk-libfprint-test',
           [ 'gtk-libfprint-test.c', 'loop.c', 'loop.h', gtk_test_resources ],
           dependencies: [ libfprint_dep, gtk_dep ],
           include_directories: [
             root_inc,
           ],
           c_args: [ common_cflags,
                     '-DPACKAGE_VERSION="' + meson.project_version() + '"' ],
           install: true,
           install_dir: bindir)

appdata = 'org.freedesktop.libfprint.Demo.appdata.xml'
install_data(appdata,
             install_dir: join_paths(datadir, 'metainfo'))

desktop = 'org.freedesktop.libfprint.Demo.desktop'
install_data(desktop,
             install_dir: join_paths(datadir, 'applications'))

icon = 'org.freedesktop.libfprint.Demo.png'
install_data(icon,
             install_dir: join_paths(datadir, 'icons'))

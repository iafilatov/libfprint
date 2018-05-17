subdir('xml')

private_headers = [
  'config.h',

  'aeslib.h',
  'assembling.h',
  'fp_internal.h',

  # Drivers
  'aes1660.h',
  'aes2501.h',
  'aes2550.h',
  'aes2660.h',
  'aes3k.h',
  'aesx660.h',
  'driver_ids.h',
  'elan.h',
  'upeksonly.h',
  'upektc.h',
  'upektc_img.h',
  'vfs0050.h',
  'vfs301_proto_fragments.h',
  'vfs301_proto.h',
  'vfs5011_proto.h',

  # NBIS
  'morph.h',
  'sunrast.h',
  'bozorth.h',
  'defs.h',
  'log.h',
  'bz_array.h',
  'lfs.h',
]

html_images = [
]

content_files = [
  'intro.xml'
]

expand_content_files = content_files

glib_prefix = dependency('glib-2.0').get_pkgconfig_variable('prefix')
glib_docpath = join_paths(glib_prefix, 'share', 'gtk-doc', 'html')
docpath = join_paths(get_option('datadir'), 'gtk-doc', 'html')

gnome.gtkdoc('libfprint',
             main_xml: 'libfprint-docs.xml',
             src_dir: join_paths(meson.source_root(), 'libfprint'),
             dependencies: libfprint_dep,
             content_files: content_files,
             expand_content_files: expand_content_files,
             scan_args: [
               '--rebuild-types',
               '--ignore-decorators=API_EXPORTED',
               '--ignore-headers=' + ' '.join(private_headers),
             ],
             fixxref_args: [
               '--html-dir=@0@'.format(docpath),
               '--extra-dir=@0@'.format(join_paths(glib_docpath, 'glib')),
               '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gobject')),
             ],
             html_assets: html_images,
             install: true)

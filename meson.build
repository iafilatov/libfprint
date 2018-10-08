project('libfprint', [ 'c', 'cpp' ],
        version: '0.8.2',
        license: 'LGPLv2.1+',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=1',
          'c_std=c99',
        ],
        meson_version: '>= 0.45.0')

add_project_arguments([ '-D_GNU_SOURCE' ], language: 'c')
add_project_arguments([ '-DG_LOG_DOMAIN="libfprint"' ], language: 'c')

libfprint_conf = configuration_data()

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
host_system = host_machine.system()

common_cflags = cc.get_supported_arguments([
    '-fgnu89-inline',
    '-fvisibility=hidden',
    '-std=gnu99',
    '-Wall',
    '-Wundef',
    '-Wunused',
    '-Wstrict-prototypes',
    '-Werror-implicit-function-declaration',
    '-Wno-pointer-sign',
    '-Wshadow'
])

# maintaining compatibility with the previous libtool versioning
# current = binary - interface
# revision = interface
soversion = 0
current = 0
revision = 0
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)

# Dependencies
glib_dep = dependency('glib-2.0', version: '>= 2.28')
libusb_dep = dependency('libusb-1.0', version: '>= 0.9.1')
mathlib_dep = cc.find_library('m', required: false)

# Drivers
drivers = get_option('drivers').split(',')
all_drivers = [ 'upekts', 'upektc', 'upeksonly', 'vcom5s', 'uru4000', 'aes1610', 'aes1660', 'aes2501', 'aes2550', 'aes2660', 'aes3500', 'aes4000', 'vfs101', 'vfs301', 'vfs5011', 'upektc_img', 'etes603', 'vfs0050', 'elan' ]
primitive_drivers = [ 'upekts' ]

if drivers == [ 'all' ]
    drivers = all_drivers
endif

if drivers.length() == 0 or drivers[0] == ''
    error('Cannot build libfprint without drivers, please specify a valid value for the drivers option')
endif

nss_dep = dependency('', required: false)
imaging_dep = dependency('', required: false)
foreach driver: drivers
    if driver == 'uru4000'
        nss_dep = dependency('nss', required: false)
        if not nss_dep.found()
            error('NSS is required for the URU4000/URU4500 driver')
        endif
    endif
    if driver == 'aes3500' or driver == 'aes4000'
        imaging_dep = dependency('pixman-1', required: false)
        if not imaging_dep.found()
            error('pixman is required for imaging support')
        endif
    endif
    if not all_drivers.contains(driver)
        error('Invalid driver \'' + driver + '\'')
    endif
endforeach

# Export the drivers' structures to the core code
drivers_struct_list = ''
drivers_img_array = 'static struct fp_img_driver * const img_drivers[] = {\n'
drivers_primitive_array = 'static struct fp_driver * const primitive_drivers[] = {\n'
foreach driver: drivers
    if primitive_drivers.contains(driver)
        drivers_struct_list += 'extern struct fp_driver ' + driver + '_driver;\n'
        drivers_primitive_array += '	&' + driver + '_driver,\n'
    else
        drivers_struct_list += 'extern struct fp_img_driver ' + driver + '_driver;\n'
        drivers_img_array += '	&' + driver + '_driver,\n'
    endif
endforeach
drivers_img_array += '};'
drivers_primitive_array += '};'

root_inc = include_directories('.')

if get_option('udev_rules')
    udev_rules_dir = get_option('udev_rules_dir')

    if udev_rules_dir == 'auto'
        udev_dep = dependency('udev')
        udev_rules_dir = udev_dep.get_pkgconfig_variable('udevdir') + '/rules.d'
    endif
endif

if get_option('x11-examples')
    x11_dep = cc.find_library('X11')
    xv_dep = dependency('xv', required: false)
    if not xv_dep.found()
        error('XV is required for X11 examples')
    endif
endif

if get_option('gtk-examples')
    gnome = import('gnome')

    gtk_dep = dependency('gtk+-3.0', required: false)
    if not gtk_dep.found()
        error('GTK+ 3.x is required for GTK+ examples')
    endif
endif

libfprint_conf.set('API_EXPORTED', '__attribute__((visibility("default")))')
configure_file(output: 'config.h', configuration: libfprint_conf)

subdir('libfprint')
subdir('examples')
if get_option('doc')
    gnome = import('gnome')
    subdir('doc')
endif
if get_option('gtk-examples')
    subdir('demo')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name: 'libfprint',
  description: 'Generic C API for fingerprint reader access',
  version: meson.project_version(),
  libraries: libfprint,
  subdirs: 'libfprint',
  filebase: 'libfprint',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
)

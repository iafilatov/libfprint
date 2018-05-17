project('libfprint', [ 'c', 'cpp' ],
        version: '0.7.0',
        license: 'LGPLv2.1+',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=1',
          'c_std=c99',
        ],
        meson_version: '>= 0.45.0')

add_project_arguments([ '-D_GNU_SOURCE' ], language: 'c')

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

if drivers == [ 'all' ]
    drivers = all_drivers
endif

nss_dep = []
imaging_dep = []
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


root_inc = include_directories('.')

if get_option('udev_rules')
    udev_rules_dir = get_option('udev_rules_dir')

    if udev_rules_dir == 'auto'
        udev_dep = dependency('udev')
        udev_rules_dir = udev_dep.get_pkgconfig_variable('udevdir') + 'rules.d'
    endif
endif

if get_option('x11-examples')
    x11_dep = cc.find_library('X11')
    xv_dep = dependency('xv', required: false)
    if not xv_dep.found()
        error('XV is required for X11 examples')
    endif
endif

# Message logging
if get_option('log')
    libfprint_conf.set('ENABLE_LOGGING', '1')
endif
if get_option('debug_log')
    libfprint_conf.set('ENABLE_DEBUG_LOGGING', '1')
endif

libfprint_conf.set('API_EXPORTED', '__attribute__((visibility("default")))')
configure_file(output: 'config.h', configuration: libfprint_conf)

subdir('libfprint')
subdir('examples')
if get_option('doc')
    gnome = import('gnome')
    subdir('doc')
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

libfprint_sources = [
    'fp_internal.h',
    'async.c',
    'core.c',
    'data.c',
    'drv.c',
    'img.c',
    'imgdev.c',
    'poll.c',
    'sync.c',
    'assembling.c',
    'assembling.h',
    'drivers/driver_ids.h',
]

nbis_sources = [
    'nbis/include/bozorth.h',
    'nbis/include/bz_array.h',
    'nbis/include/defs.h',
    'nbis/include/lfs.h',
    'nbis/include/log.h',
    'nbis/include/morph.h',
    'nbis/include/sunrast.h',
    'nbis/bozorth3/bozorth3.c',
    'nbis/bozorth3/bz_alloc.c',
    'nbis/bozorth3/bz_drvrs.c',
    'nbis/bozorth3/bz_gbls.c',
    'nbis/bozorth3/bz_io.c',
    'nbis/bozorth3/bz_sort.c',
    'nbis/mindtct/binar.c',
    'nbis/mindtct/block.c',
    'nbis/mindtct/contour.c',
    'nbis/mindtct/detect.c',
    'nbis/mindtct/dft.c',
    'nbis/mindtct/free.c',
    'nbis/mindtct/globals.c',
    'nbis/mindtct/imgutil.c',
    'nbis/mindtct/init.c',
    'nbis/mindtct/line.c',
    'nbis/mindtct/log.c',
    'nbis/mindtct/loop.c',
    'nbis/mindtct/maps.c',
    'nbis/mindtct/matchpat.c',
    'nbis/mindtct/minutia.c',
    'nbis/mindtct/morph.c',
    'nbis/mindtct/quality.c',
    'nbis/mindtct/remove.c',
    'nbis/mindtct/ridges.c',
    'nbis/mindtct/shape.c',
    'nbis/mindtct/sort.c',
    'nbis/mindtct/util.c',
]

aeslib = false
aesx660 = false
aes3k = false
drivers_sources = []
drivers_cflags = []
foreach driver: drivers
    if driver == 'upekts'
        drivers_sources += [ 'drivers/upekts.c' ]
    endif
    if driver == 'upektc'
        drivers_sources += [ 'drivers/upektc.c', 'drivers/upektc.h' ]
    endif
    if driver == 'upeksonly'
        drivers_sources += [ 'drivers/upeksonly.c', 'drivers/upeksonly.h' ]
    endif
    if driver == 'uru4000'
        drivers_sources += [ 'drivers/uru4000.c' ]
    endif
    if driver == 'aes1610'
        drivers_sources += [ 'drivers/aes1610.c' ]
        aeslib = true
    endif
    if driver == 'aes1660'
        drivers_sources += [ 'drivers/aes1660.c', 'drivers/aes1660.h' ]
        aeslib = true
        aesx660 = true
    endif
    if driver == 'aes2501'
        drivers_sources += [ 'drivers/aes2501.c', 'drivers/aes2501.h' ]
        aeslib = true
    endif
    if driver == 'aes2550'
        drivers_sources += [ 'drivers/aes2550.c', 'drivers/aes2550.h' ]
        aeslib = true
    endif
    if driver == 'aes2660'
        drivers_sources += [ 'drivers/aes2660.c', 'drivers/aes2660.h' ]
        aeslib = true
        aesx660 = true
    endif
    if driver == 'aes3500'
        drivers_sources += [ 'drivers/aes3500.c' ]
        aeslib = true
        aes3k = true
    endif
    if driver == 'aes4000'
        drivers_sources += [ 'drivers/aes4000.c' ]
        aeslib = true
        aes3k = true
    endif
    if driver == 'fdu2000'
        drivers_sources += [ 'drivers/fdu2000.c' ]
    endif
    if driver == 'vcom5s'
        drivers_sources += [ 'drivers/vcom5s.c' ]
    endif
    if driver == 'vfs101'
        drivers_sources += [ 'drivers/vfs101.c' ]
    endif
    if driver == 'vfs301'
        drivers_sources += [ 'drivers/vfs301.c', 'drivers/vfs301_proto.c',  'drivers/vfs301_proto.h', 'drivers/vfs301_proto_fragments.h' ]
    endif
    if driver == 'vfs5011'
        drivers_sources += [ 'drivers/vfs5011.c', 'drivers/vfs5011_proto.h' ]
    endif
    if driver == 'upektc_img'
        drivers_sources += [ 'drivers/upektc_img.c', 'drivers/upektc_img.h' ]
    endif
    if driver == 'etes603'
        drivers_sources += [ 'drivers/etes603.c' ]
    endif
    if driver == 'vfs0050'
        drivers_sources += [ 'drivers/vfs0050.c', 'drivers/vfs0050.h' ]
    endif
    if driver == 'elan'
        drivers_sources += [ 'drivers/elan.c', 'drivers/elan.h' ]
    endif
    drivers_cflags += [ '-DENABLE_' + driver.to_upper() + '=1' ]
endforeach

if aeslib
    drivers_sources += [ 'aeslib.c', 'aeslib.h' ]
endif
if aesx660
    drivers_sources += ['drivers/aesx660.c', 'drivers/aesx660.h' ]
endif
if aes3k
    drivers_sources += ['drivers/aes3k.c', 'drivers/aes3k.h' ]
endif

other_sources = []
if imaging_dep.found()
    other_sources += [ 'pixman.c' ]
endif

deps = [ mathlib_dep, glib_dep, libusb_dep, nss_dep, imaging_dep ]
libfprint = library('fprint',
                    libfprint_sources + drivers_sources + nbis_sources + other_sources,
                    soversion: soversion,
                    version: libversion,
                    c_args: common_cflags + drivers_cflags,
                    include_directories: [
                      root_inc,
                      include_directories('nbis/include'),
                    ],
                    dependencies: deps,
                    install: true)

libfprint_dep = declare_dependency(link_with: libfprint,
                                   include_directories: root_inc)

install_headers(['fprint.h'])

udev_rules = executable('fprint-list-udev-rules',
                        'fprint-list-udev-rules.c',
                        include_directories: [
                          root_inc,
                        ],
                        dependencies: [ deps, libfprint_dep ],
                        install: false)

if get_option('udev_rules')
    custom_target('udev-rules',
                  output: '60-fprint-autosuspend.rules',
                  capture: true,
                  command: [ udev_rules ],
                  install: true,
                  install_dir: udev_rules_dir)
endif

# MOSTLYCLEANFILES = $(udev_rules_DATA)

# EXTRA_DIST = \
#	60-fprint-autosuspend.rules

# udev_rules_DATA = 60-fprint-autosuspend.rules

# if ENABLE_UDEV_RULES
# $(udev_rules_DATA): fprint-list-udev-rules
#	$(builddir)/fprint-list-udev-rules > $@
#endif

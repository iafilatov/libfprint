lib_LTLIBRARIES = libfprint.la

UPEKTS_SRC = drivers/upekts.c
URU4000_SRC = drivers/uru4000.c
AES4000_SRC = drivers/aes4000.c

DRIVER_SRC = $(UPEKTS_SRC) $(URU4000_SRC) $(AES4000_SRC)

NBIS_SRC = \
	nbis/bozorth3/bozorth3.c \
	nbis/bozorth3/bz_alloc.c \
	nbis/bozorth3/bz_drvrs.c \
	nbis/bozorth3/bz_gbls.c \
	nbis/bozorth3/bz_io.c \
	nbis/bozorth3/bz_sort.c \
	nbis/mindtct/binar.c \
	nbis/mindtct/block.c \
	nbis/mindtct/chaincod.c \
	nbis/mindtct/contour.c \
	nbis/mindtct/detect.c \
	nbis/mindtct/dft.c \
	nbis/mindtct/free.c \
	nbis/mindtct/getmin.c \
	nbis/mindtct/globals.c \
	nbis/mindtct/imgutil.c \
	nbis/mindtct/init.c \
	nbis/mindtct/isempty.c \
	nbis/mindtct/line.c \
	nbis/mindtct/link.c \
	nbis/mindtct/log.c \
	nbis/mindtct/loop.c \
	nbis/mindtct/maps.c \
	nbis/mindtct/matchpat.c \
	nbis/mindtct/minutia.c \
	nbis/mindtct/morph.c \
	nbis/mindtct/mytime.c \
	nbis/mindtct/quality.c \
	nbis/mindtct/remove.c \
	nbis/mindtct/results.c \
	nbis/mindtct/ridges.c \
	nbis/mindtct/shape.c \
	nbis/mindtct/sort.c \
	nbis/mindtct/util.c \
	nbis/mindtct/xytreps.c

libfprint_la_CFLAGS = -fvisibility=hidden -Inbis/include $(LIBUSB_CFLAGS) $(GLIB_CFLAGS) $(AM_CFLAGS)
libfprint_la_LDFLAGS = -version-info @lt_major@:@lt_revision@:@lt_age@
libfprint_la_LIBADD = -lm $(LIBUSB_LIBS) $(GLIB_LIBS)

libfprint_la_SOURCES =	\
	core.c		\
	data.c		\
	img.c		\
	imgdev.c	\
	$(DRIVER_SRC)	\
	$(NBIS_SRC)


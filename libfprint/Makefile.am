lib_LTLIBRARIES = libfprint.la

UPEKTS_SRC = drivers/upekts.c
UPEKTC_SRC = drivers/upektc.c
URU4000_SRC = drivers/uru4000.c
AES1610_SRC = drivers/aes1610.c
AES2501_SRC = drivers/aes2501.c drivers/aes2501.h
AES4000_SRC = drivers/aes4000.c
FDU2000_SRC = drivers/fdu2000.c

DRIVER_SRC = $(UPEKTS_SRC) $(URU4000_SRC) $(AES1610_SRC) $(AES2501_SRC) $(AES4000_SRC) $(UPEKTC_SRC) $(FDU2000_SRC)

NBIS_SRC = \
	nbis/include/bozorth.h \
	nbis/include/bz_array.h \
	nbis/include/defs.h \
	nbis/include/lfs.h \
	nbis/include/log.h \
	nbis/include/morph.h \
	nbis/include/sunrast.h \
	nbis/bozorth3/bozorth3.c \
	nbis/bozorth3/bz_alloc.c \
	nbis/bozorth3/bz_drvrs.c \
	nbis/bozorth3/bz_gbls.c \
	nbis/bozorth3/bz_io.c \
	nbis/bozorth3/bz_sort.c \
	nbis/mindtct/binar.c \
	nbis/mindtct/block.c \
	nbis/mindtct/contour.c \
	nbis/mindtct/detect.c \
	nbis/mindtct/dft.c \
	nbis/mindtct/free.c \
	nbis/mindtct/globals.c \
	nbis/mindtct/imgutil.c \
	nbis/mindtct/init.c \
	nbis/mindtct/line.c \
	nbis/mindtct/log.c \
	nbis/mindtct/loop.c \
	nbis/mindtct/maps.c \
	nbis/mindtct/matchpat.c \
	nbis/mindtct/minutia.c \
	nbis/mindtct/morph.c \
	nbis/mindtct/quality.c \
	nbis/mindtct/remove.c \
	nbis/mindtct/ridges.c \
	nbis/mindtct/shape.c \
	nbis/mindtct/sort.c \
	nbis/mindtct/util.c

libfprint_la_CFLAGS = -fvisibility=hidden -I$(srcdir)/nbis/include $(LIBUSB_CFLAGS) $(GLIB_CFLAGS) $(IMAGEMAGICK_CFLAGS) $(CRYPTO_CFLAGS) $(AM_CFLAGS)
libfprint_la_LDFLAGS = -version-info @lt_major@:@lt_revision@:@lt_age@
libfprint_la_LIBADD = -lm $(LIBUSB_LIBS) $(GLIB_LIBS) $(IMAGEMAGICK_LIBS) $(CRYPTO_LIBS)

libfprint_la_SOURCES =	\
	fp_internal.h	\
	core.c		\
	data.c		\
	img.c		\
	imgdev.c	\
	aeslib.c	\
	aeslib.h	\
	$(DRIVER_SRC)	\
	$(NBIS_SRC)

pkginclude_HEADERS = fprint.h

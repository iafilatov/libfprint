lib_LTLIBRARIES = libfprint.la

libfprint_la_CFLAGS = -fvisibility=hidden $(LIBUSB_CFLAGS) $(GLIB_CFLAGS)
libfprint_la_LDFLAGS = -version-info @lt_major@:@lt_revision@:@lt_age@
libfprint_la_LIBADD = $(LIBUSB_LIBS) $(GLIB_LIBS)

libfprint_la_SOURCES = \
	core.c


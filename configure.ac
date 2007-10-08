AC_INIT([libfprint], [0.0])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([libfprint/core.c])
AM_CONFIG_HEADER([config.h])

AC_PREREQ([2.61])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_C_INLINE

# Library versioning
lt_major="0"
lt_revision="0"
lt_age="0"
AC_SUBST(lt_major)
AC_SUBST(lt_revision)
AC_SUBST(lt_age)

PKG_CHECK_MODULES(LIBUSB, "libusb")
AC_SUBST(LIBUSB_CFLAGS)
AC_SUBST(LIBUSB_LIBS)

PKG_CHECK_MODULES(GLIB, "glib-2.0")
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

# Examples build
AC_ARG_ENABLE([examples-build], [AS_HELP_STRING([--enable-examples-build],
	[build example applications (default n)])],
	[build_examples=$enableval],
	[build_examples='no'])
AM_CONDITIONAL([BUILD_EXAMPLES], [test "x$build_examples" != "xno"])

AC_DEFINE([API_EXPORTED], [__attribute__((visibility("default")))], [Default visibility])

AC_CONFIG_FILES([Makefile] [libfprint/Makefile] [examples/Makefile])
AC_OUTPUT


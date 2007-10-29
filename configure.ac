AC_INIT([libfprint], [0.0])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([libfprint/core.c])
AM_CONFIG_HEADER([config.h])

AC_PREREQ([2.50])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_C_INLINE
AM_PROG_CC_C_O

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

# Examples build
AC_ARG_ENABLE([x11-examples-build], [AS_HELP_STRING([--enable-x11-examples-build],
	[build X11 example applications (default n)])],
	[build_x11_examples=$enableval],
	[build_x11_examples='no'])
AM_CONDITIONAL([BUILD_X11_EXAMPLES], [test "x$build_x11_examples" != "xno"])


if test "x$build_x11_examples" != "xno"; then
	# check for Xv extensions
	# imported from Coriander
	AC_DEFUN([AC_CHECK_XV],[
		AC_SUBST(XV_CFLAGS)
		AC_SUBST(XV_LIBS)
		AC_MSG_CHECKING(for Xv extensions)
		AC_TRY_COMPILE([
		#include <X11/Xlib.h>
	 	#include <X11/extensions/Xvlib.h>],[
		int main(void) { (void) XvGetPortAttribute(0, 0, 0, 0); return 0; }
		],xv=yes,xv=no);
		AC_MSG_RESULT($xv)
	 	if test x$xv = xyes; then
    	       XV_LIBS="-lXv -lXext"
        	   XV_CFLAGS=""
		   AC_DEFINE(HAVE_XV,1,[defined if XV video overlay is available])
		else
			AC_MSG_ERROR([XV is required for X11 examples])
 		fi
	])
	AC_CHECK_XV
fi

# Message logging
AC_ARG_ENABLE([log], [AS_HELP_STRING([--disable-log], [disable all logging])],
	[log_enabled=$enableval],
	[log_enabled='yes'])
if test "x$log_enabled" != "xno"; then
	AC_DEFINE([ENABLE_LOGGING], 1, [Message logging])
fi

AC_ARG_ENABLE([debug-log], [AS_HELP_STRING([--enable-debug-log],
	[enable debug logging (default n)])],
	[debug_log_enabled=$enableval],
	[debug_log_enabled='no'])
if test "x$debug_log_enabled" != "xno"; then
	AC_DEFINE([ENABLE_DEBUG_LOGGING], 1, [Debug message logging])
fi


AC_DEFINE([API_EXPORTED], [__attribute__((visibility("default")))], [Default visibility])
AM_CFLAGS="-Werror-implicit-function-declaration -Wimplicit-int -Wunreachable-code -Wunused-function -Wunused-label -Wunused-value -Wunused-variable -Wnonnull -Wreturn-type -Wextra -Wshadow"
AC_SUBST(AM_CFLAGS)

AC_CONFIG_FILES([Makefile] [libfprint/Makefile] [examples/Makefile])
AC_OUTPUT


INCLUDES = -I$(top_srcdir)
noinst_PROGRAMS = verify_live enroll verify img_capture

verify_live_SOURCES = verify_live.c
verify_live_LDADD = ../libfprint/libfprint.la -lfprint

enroll_SOURCES = enroll.c
enroll_LDADD = ../libfprint/libfprint.la -lfprint

verify_SOURCES = verify.c
verify_LDADD = ../libfprint/libfprint.la -lfprint

img_capture_SOURCES = img_capture.c
img_capture_LDADD = ../libfprint/libfprint.la -lfprint

if BUILD_X11_EXAMPLES
noinst_PROGRAMS += img_capture_continuous

img_capture_continuous_CFLAGS = $(X_CFLAGS) $(XV_CFLAGS)
img_capture_continuous_SOURCES = img_capture_continuous.c
img_capture_continuous_LDADD = ../libfprint/libfprint.la -lfprint $(X_LIBS) $(X_PRE_LIBS) $(XV_LIBS) -lX11 $(X_EXTRA_LIBS);
endif


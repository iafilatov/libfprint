EXTRA_DIST = THANKS TODO HACKING libfprint.pc.in

SUBDIRS = libfprint doc

if BUILD_EXAMPLES
SUBDIRS += examples
endif

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=libfprint.pc
DISTCLEANFILES=libfprint.pc

.PHONY: ChangeLog
ChangeLog:
	git --git-dir $(top_srcdir)/.git log > ChangeLog

dist-hook: ChangeLog


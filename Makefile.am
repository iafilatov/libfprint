AUTOMAKE_OPTIONS = dist-bzip2
EXTRA_DIST = THANKS TODO HACKING libfprint.pc.in
DISTCLEANFILES = ChangeLog libfprint.pc

SUBDIRS = libfprint doc

if BUILD_EXAMPLES
SUBDIRS += examples
endif

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=libfprint.pc

.PHONY: ChangeLog dist-up
ChangeLog:
	git --git-dir $(top_srcdir)/.git log > ChangeLog || touch ChangeLog

dist-hook: ChangeLog

dist-up: dist
	ncftpput upload.sourceforge.net incoming $(distdir).tar.bz2

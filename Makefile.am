AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6
ACLOCAL_AMFLAGS = -I m4

INCLUDES = $(all_includes) -I$(top_srcdir)/include
SUBDIRS = include src tests

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libosmocore.pc libosmocodec.pc libosmovty.pc

BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

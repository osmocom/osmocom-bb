# This is _NOT_ the library release version, it's an API version.
# Please read chapter "Library interface versions" of the libtool documentation
# before making any modifications: https://www.gnu.org/software/libtool/manual/html_node/Versioning.html
LIBVERSION=0:0:0

AM_CPPFLAGS = -I$(top_srcdir)/include $(TALLOC_CFLAGS)
AM_CFLAGS = -Wall

lib_LTLIBRARIES = libosmocodec.la

libosmocodec_la_SOURCES = gsm610.c gsm620.c gsm660.c gsm690.c
libosmocodec_la_LDFLAGS = -version-info $(LIBVERSION) -no-undefined
libosmocodec_la_LIBADD = $(top_builddir)/src/libosmocore.la

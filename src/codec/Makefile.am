# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall

lib_LTLIBRARIES = libosmocodec.la

libosmocodec_la_SOURCES = gsm610.c gsm620.c gsm660.c gsm690.c
libosmocodec_la_LDFLAGS = -version-info $(LIBVERSION)

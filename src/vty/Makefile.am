# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

AM_CFLAGS = -Wall $(all_includes) -I$(top_srcdir)/include

if ENABLE_VTY
lib_LTLIBRARIES = libosmovty.la

libosmovty_la_SOURCES = buffer.c command.c vty.c vector.c utils.c \
			telnet_interface.c logging_vty.c
libosmovty_la_LDFLAGS = -version-info $(LIBVERSION) -no-undefined
libosmovty_la_LIBADD = $(top_builddir)/src/libosmocore.la
endif

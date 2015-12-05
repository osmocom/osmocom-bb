# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=3:0:0

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include
AM_CFLAGS = -Wall $(TALLOC_CFLAGS)

if ENABLE_VTY
lib_LTLIBRARIES = libosmovty.la

libosmovty_la_SOURCES = buffer.c command.c vty.c vector.c utils.c \
			telnet_interface.c logging_vty.c stats_vty.c
libosmovty_la_LDFLAGS = -version-info $(LIBVERSION) -no-undefined $(TALLOC_LIBS)
libosmovty_la_LIBADD = $(top_builddir)/src/libosmocore.la
endif

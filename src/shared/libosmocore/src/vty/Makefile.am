# This is _NOT_ the library release version, it's an API version.
# Please read chapter "Library interface versions" of the libtool documentation
# before making any modifications: https://www.gnu.org/software/libtool/manual/html_node/Versioning.html
LIBVERSION=3:0:0

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include
AM_CFLAGS = -Wall $(TALLOC_CFLAGS)

if ENABLE_VTY
lib_LTLIBRARIES = libosmovty.la

libosmovty_la_SOURCES = buffer.c command.c vty.c vector.c utils.c \
			telnet_interface.c logging_vty.c stats_vty.c fsm_vty.c
libosmovty_la_LDFLAGS = -version-info $(LIBVERSION) -no-undefined $(TALLOC_LIBS)
libosmovty_la_LIBADD = $(top_builddir)/src/libosmocore.la
endif

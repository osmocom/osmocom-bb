# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

AM_CFLAGS = -Wall $(all_includes) -I$(top_srcdir)/include -I$(top_builddir)/include $(TALLOC_CFLAGS)

lib_LTLIBRARIES = libosmoctrl.la

libosmoctrl_la_SOURCES = control_cmd.c control_if.c

libosmoctrl_la_LDFLAGS = $(LTLDFLAGS_OSMOCTRL) $(TALLOC_LIBS) -version-info $(LIBVERSION) -no-undefined
libosmoctrl_la_LIBADD = \
	$(top_builddir)/src/libosmocore.la \
	$(top_builddir)/src/gsm/libosmogsm.la \
	$(top_builddir)/src/vty/libosmovty.la

if ENABLE_VTY
libosmoctrl_la_SOURCES += control_vty.c
endif

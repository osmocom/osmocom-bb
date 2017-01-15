# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=4:0:0

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include
AM_CFLAGS = -Wall ${GCC_FVISIBILITY_HIDDEN} -fno-strict-aliasing $(TALLOC_CFLAGS)

# FIXME: this should eventually go into a milenage/Makefile.am
noinst_HEADERS = common_vty.h

if ENABLE_GB
lib_LTLIBRARIES = libosmogb.la

libosmogb_la_LDFLAGS = $(LTLDFLAGS_OSMOGB) -version-info $(LIBVERSION) $(TALLOC_LIBS)
libosmogb_la_LIBADD = \
		$(top_builddir)/src/libosmocore.la \
		$(top_builddir)/src/vty/libosmovty.la \
		$(top_builddir)/src/gsm/libosmogsm.la

libosmogb_la_SOURCES = gprs_ns.c gprs_ns_frgre.c gprs_ns_vty.c \
		  gprs_bssgp.c gprs_bssgp_util.c gprs_bssgp_vty.c \
		  gprs_bssgp_bss.c common_vty.c
endif

EXTRA_DIST = libosmogb.map


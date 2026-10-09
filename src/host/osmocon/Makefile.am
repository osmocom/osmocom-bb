AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

# versioning magic
BUILT_SOURCES = $(top_srcdir)/.version
EXTRA_DIST = .version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

AM_CPPFLAGS = -I$(srcdir) -DHOST_BUILD
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

# headers are actually symlinks to the firmware
noinst_HEADERS = sercomm.h loader_protocol.h

sbin_PROGRAMS = osmocon osmoload

# FIXME: sercomm needs to move into libosmocore or another shared lib
osmocon_SOURCES = osmocon.c tpu_debug.c sercomm.c
osmocon_LDADD = $(LIBOSMOCORE_LIBS)

osmoload_SOURCE = osmoload.c sercomm.c
osmoload_LDADD = $(LIBOSMOCORE_LIBS)

AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

# versioning magic
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

sbin_PROGRAMS = osmocon osmoload

# FIXME: sercomm needs to move into libosmocore or another shared lib
INCLUDES += -I../../target/firmware/include/comm -I../../target/firmware/apps -DHOST_BUILD
osmocon_SOURCES = osmocon.c tpu_debug.c ../../target/firmware/comm/sercomm.c
osmocon_LDADD = $(LIBOSMOCORE_LIBS)

osmoload_SOURCE = osmoload.c ../../target/firmware/comm/sercomm.c
osmoload_LDADD = $(LIBOSMOCORE_LIBS)

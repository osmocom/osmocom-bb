AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

sbin_PROGRAMS = osmocon

# FIXME: sercomm needs to move into libosmocore or another shared lib
INCLUDES += -I../../target/firmware/include/comm -DHOST_BUILD
osmocon_SOURCES = osmocon.c tpu_debug.c ../../target/firmware/comm/sercomm.c
osmocon_LDADD = $(LIBOSMOCORE_LIBS)


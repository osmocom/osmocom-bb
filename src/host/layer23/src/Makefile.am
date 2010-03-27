INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

sbin_PROGRAMS = layer23
layer23_SOURCES = main.c l1ctl.c gsmtap_util.c lapdm.c rslms.c \
		  layer3.c logging.c
layer23_LDADD = $(LIBOSMOCORE_LIBS)

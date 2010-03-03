INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

noinst_LIBRARIES = libosmocom.a
libosmocom_a_SOURCES = libosmocom/debug.c 

sbin_PROGRAMS = layer23
layer23_SOURCES = main.c l1ctl.c gsmtap_util.c lapdm.c rslms.c layer3.c
layer23_LDADD = libosmocom.a $(LIBOSMOCORE_LIBS)

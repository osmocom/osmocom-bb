INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)

noinst_LIBRARIES = libosmocom.a
libosmocom_a_SOURCES = libosmocom/debug.c 

sbin_PROGRAMS = layer2
layer2_SOURCES = layer2_main.c osmocom_layer2.c gsmtap_util.c
layer2_LDADD = libosmocom.a $(LIBOSMOCORE_LIBS)

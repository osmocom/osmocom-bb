INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall

noinst_LIBRARIES = libosmocom.a
libosmocom_a_SOURCES = libosmocom/debug.c libosmocom/msgb.c libosmocom/select.c  \
		       libosmocom/talloc.c libosmocom/timer.c

sbin_PROGRAMS = layer2
layer2_SOURCES = layer2_main.c osmocom_layer2.c
layer2_LDADD = libosmocom.a

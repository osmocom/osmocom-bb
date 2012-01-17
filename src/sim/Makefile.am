# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=0:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -fPIC -Wall

noinst_HEADERS = sim_int.h gsm_int.h

lib_LTLIBRARIES = libosmosim.la

libosmosim_la_SOURCES = core.c card_fs_sim.c card_fs_usim.c card_fs_uicc.c file_codec.c reader.c reader_pcsc.c
# FIXME: use autoconf to determine pcsc include path / library name
libosmosim_la_LDFLAGS = -version-info $(LIBVERSION) -lpcsclite
libosmosim_la_CFLAGS = -I/usr/include/PCSC


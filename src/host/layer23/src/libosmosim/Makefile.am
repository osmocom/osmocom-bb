INCLUDES = $(all_includes) -I$(top_srcdir)/include -I/home/gsmmap/jdk1.7.0_45/include -I/home/gsmmap/jdk1.7.0_45/include/linux
AM_CFLAGS = -Wall -g $(LIBOSMOCORE_CFLAGS)
LDADD = $(LIBOSMOCORE_LIBS)

lib_LTLIBRARIES = libosmosim.la
libosmosim_la_SOURCES = ../common/l1ctl.c ../common/logging.c ../common/l1l2_interface.c libosmosim.c
libosmosim_la_LIBADD = $(LDADD)

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall $(LIBOSMOCORE_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS) $(LIBOSMOVTY_LIBS)

noinst_LIBRARIES = libmobile.a
libmobile_a_SOURCES = gsm322.c gsm48_cc.c gsm48_mm.c gsm48_rr.c	\
	mnccms.c settings.c subscriber.c support.c	\
	sysinfo.c transaction.c vty_interface.c

bin_PROGRAMS = mobile

mobile_SOURCES = ../common/main.c app_mobile.c
mobile_LDADD = libmobile.a $(LDADD)



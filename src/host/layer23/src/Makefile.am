INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)
#AM_LDFLAGS = $(LIBOSMOCORE_LIBS)

noinst_LIBRARIES = liblayer23.a libmobile.a
liblayer23_a_SOURCES = l1ctl.c l1l2_interface.c lapdm.c logging.c main.c
libmobile_a_SOURCES = gsm322.c gsm48_cc.c gsm48_mm.c gsm48_rr.c	\
	mnccms.c networks.c settings.c subscriber.c support.c	\
	sysinfo.c transaction.c vty_interface.c

bin_PROGRAMS = bcch_scan layer23 echo_test mobile

bcch_scan_SOURCES = main.c app_bcch_scan.c bcch_scan.c
bcch_scan_LDADD = liblayer23.a $(LIBOSMOCORE_LIBS)

layer23_SOURCES = main.c app_phone.c layer3.c rslms.c
layer23_LDADD = liblayer23.a $(LIBOSMOCORE_LIBS)

echo_test_SOURCES = main.c app_echo_test.c
echo_test_LDADD = liblayer23.a $(LIBOSMOCORE_LIBS)

mobile_SOURCES = main.c app_mobile.c
mobile_LDADD = liblayer23.a libmobile.a $(LIBOSMOCORE_LIBS) $(LIBOSMOVTY_LIBS)



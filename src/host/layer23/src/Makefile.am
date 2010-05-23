INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)
#AM_LDFLAGS = $(LIBOSMOCORE_LIBS)

noinst_LIBRARIES = liblayer23.a libvty.a
liblayer23_a_SOURCES = l1l2_interface.c l1ctl.c gsmtap_util.c lapdm.c rslms.c \
			layer3.c logging.c bcch_scan.c settings.c \
			gsm48_cc.c transaction.c gsm48_mm.c gsm48_rr.c \
			gsm322.c support.c subscriber.c sysinfo.c networks.c \
			mnccms.c telnet_interface.c vty_interface.c

bin_PROGRAMS = bcch_scan layer23 echo_test mobile

bcch_scan_SOURCES = main.c app_bcch_scan.c
bcch_scan_LDADD = liblayer23.a $(LIBOSMOCORE_LIBS)

layer23_SOURCES = main.c app_phone.c
layer23_LDADD = liblayer23.a $(LIBOSMOCORE_LIBS)

echo_test_SOURCES = main.c app_echo_test.c
echo_test_LDADD = liblayer23.a $(LIBOSMOCORE_LIBS)

mobile_SOURCES = main.c app_mobile.c
mobile_LDADD = liblayer23.a libvty.a $(LIBOSMOCORE_LIBS)

libvty_a_SOURCES = vty/buffer.c vty/command.c vty/vector.c vty/vty.c


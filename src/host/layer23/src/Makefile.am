INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS)
AM_LDFLAGS = $(LIBOSMOCORE_LIBS)

noinst_LIBRARIES = liblayer23.a
liblayer23_a_SOURCES = l1ctl.c gsmtap_util.c lapdm.c rslms.c \
			layer3.c logging.c

bin_PROGRAMS = bcch_scan layer23 echo_test

bcch_scan_SOURCES = main.c app_bcch_scan.c
bcch_scan_LDADD = liblayer23.a

layer23_SOURCES = main.c app_phone.c
layer23_LDADD = liblayer23.a

echo_test_SOURCES = main.c app_echo_test.c
echo_test_LDADD = liblayer23.a

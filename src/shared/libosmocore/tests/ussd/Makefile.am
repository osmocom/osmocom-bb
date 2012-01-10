INCLUDES = $(all_includes) -I$(top_srcdir)/include
noinst_PROGRAMS = ussd_test
EXTRA_DIST = ussd_test.ok

ussd_test_SOURCES = ussd_test.c
ussd_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

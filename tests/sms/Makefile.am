INCLUDES = $(all_includes) -I$(top_srcdir)/include
noinst_PROGRAMS = sms_test
EXTRA_DIST = sms_test.ok

sms_test_SOURCES = sms_test.c
sms_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

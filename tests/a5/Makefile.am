INCLUDES = $(all_includes) -I$(top_srcdir)/include
noinst_PROGRAMS = a5_test
EXTRA_DIST = a5_test.ok

a5_test_SOURCES = a5_test.c
a5_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

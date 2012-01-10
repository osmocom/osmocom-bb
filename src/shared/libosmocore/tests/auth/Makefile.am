INCLUDES = $(all_includes) -I$(top_srcdir)/include
noinst_PROGRAMS = milenage_test
EXTRA_DIST = milenage_test.ok

milenage_test_SOURCES = milenage_test.c
milenage_test_LDADD = $(top_builddir)/src/libosmocore.la \
			$(top_builddir)/src/gsm/libosmogsm.la


INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_FLAGS = -Wall -O0
noinst_PROGRAMS = lapd_test
EXTRA_DIST = lapd_test.ok

lapd_test_SOURCES = lapd_test.c
lapd_test_LDADD = \
	$(top_builddir)/src/libosmocore.la \
	$(top_builddir)/src/gsm/libosmogsm.la

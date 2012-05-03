INCLUDES = $(all_includes) -I$(top_srcdir)/include
noinst_PROGRAMS = gsm0808_test
EXTRA_DIST = gsm0808_test.ok

gsm0808_test_SOURCES = gsm0808_test.c
gsm0808_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

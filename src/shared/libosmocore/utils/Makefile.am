if ENABLE_UTILITIES
INCLUDES = $(all_includes) -I$(top_srcdir)/include
noinst_PROGRAMS = osmo-arfcn

osmo_arfcn_SOURCES = osmo-arfcn.c
osmo_arfcn_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la
endif

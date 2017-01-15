if ENABLE_UTILITIES
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include $(TALLOC_CFLAGS)
AM_CFLAGS = -Wall

bin_PROGRAMS = osmo-arfcn osmo-auc-gen

osmo_arfcn_SOURCES = osmo-arfcn.c
osmo_arfcn_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

osmo_auc_gen_SOURCES = osmo-auc-gen.c
osmo_auc_gen_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la

if ENABLE_PCSC
noinst_PROGRAMS = osmo-sim-test
osmo_sim_test_SOURCES = osmo-sim-test.c
osmo_sim_test_LDADD = $(top_builddir)/src/libosmocore.la $(top_builddir)/src/gsm/libosmogsm.la $(top_builddir)/src/sim/libosmosim.la $(PCSC_LIBS)
osmo_sim_test_CFLAGS = $(PCSC_CFLAGS)
endif
endif

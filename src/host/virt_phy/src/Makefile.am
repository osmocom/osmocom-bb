OSMOCOM_DIR = /home/basti/Osmocom
OSMO_BB_DIR = $(OSMOCOM_DIR)/osmocom-bb
OSMO_BTS_DIR = $(OSMOCOM_DIR)/osmo-bts
OPENBSC_DIR = $(OSMOCOM_DIR)/openbsc

AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS)
AM_CPPFLAGS = $(all_includes) -I$(top_srcdir)/include -I$(OSMO_BB_DIR)/include -I$(OSMO_BTS_DIR)/include -I$(OPENBSC_DIR)/openbsc/include -I$(OSMO_BB_DIR)/src/host/layer23/include
# TODO: somehow this include path causes errors, thus the needed files are copied into $(top_srcdir)/include. Would be better to include directly from $(OSMO_BB_DIR)/src/target/firmware/include to avoid redundancy.
# -I$(OSMO_BB_DIR)/src/target/firmware/include

# disable optimization and enable debugging
CFLAGS = -g -O0

sbin_PROGRAMS = virtphy
virtphy_SOURCES = virtphy.c l1ctl_sock.c l1ctl_sap.c gsmtapl1_if.c logging.c virt_l1_model.c virtual_um.c osmo_mcast_sock.c
virtphy_LDADD = $(LIBOSMOCORE_LIBS) $(LIBOSMOGSM_LIBS)

# debug output
all:
	$(info $$AM_CPPFLAGS is [${AM_CPPFLAGS}])

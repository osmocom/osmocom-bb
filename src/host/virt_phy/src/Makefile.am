AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS)
AM_CPPFLAGS = $(all_includes) -I$(top_srcdir)/include  -I$(top_srcdir)/../layer23/include

sbin_PROGRAMS = virtphy
virtphy_SOURCES = virtphy.c l1ctl_sock.c l1ctl_sap.c gsmtapl1_if.c logging.c virt_l1_model.c shared/virtual_um.c shared/osmo_mcast_sock.c
virtphy_LDADD = $(LIBOSMOCORE_LIBS) $(LIBOSMOGSM_LIBS)

# debug output
all:
	$(info $$AM_CPPFLAGS is [${AM_CPPFLAGS}])

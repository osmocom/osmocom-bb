AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS)
AM_CPPFLAGS = $(all_includes) -I$(top_srcdir)/include

bin_PROGRAMS = virtphy
virtphy_SOURCES = virtphy.c l1ctl_sock.c gsmtapl1_if.c l1ctl_sap.c virt_prim_pm.c virt_prim_fbsb.c virt_prim_rach.c virt_prim_data.c virt_prim_traffic.c virt_l1_sched_simple.c logging.c virt_l1_model.c shared/virtual_um.c shared/osmo_mcast_sock.c
virtphy_LDADD = $(LIBOSMOCORE_LIBS) $(LIBOSMOGSM_LIBS) 
virtphy_LDFLAGS = -pthread

# debug output
all:
	$(info $$AM_CPPFLAGS is [${AM_CPPFLAGS}])

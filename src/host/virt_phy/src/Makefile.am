AM_CFLAGS=-Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS)
AM_CPPFLAGS = $(all_includes) -I$(top_srcdir)/include

bin_PROGRAMS = virtphy

virtphy_SOURCES = \
	virtphy.c \
	l1gprs.c \
	logging.c \
	gsmtapl1_if.c \
	l1ctl_sock.c \
	l1ctl_sap.c \
	virt_prim_pm.c \
	virt_prim_fbsb.c \
	virt_prim_rach.c \
	virt_prim_data.c \
	virt_prim_pdch.c \
	virt_prim_traffic.c \
	virt_l1_sched_simple.c \
	virt_l1_model.c \
	shared/virtual_um.c \
	shared/osmo_mcast_sock.c \
	$(NULL)

virtphy_LDADD = \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(NULL)

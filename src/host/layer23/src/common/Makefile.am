AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBOSMOGPRSRLCMAC_CFLAGS) \
	$(LIBOSMOGPRSLLC_CFLAGS) \
	$(LIBOSMOGPRSSNDCP_CFLAGS) \
	$(LIBGPS_CFLAGS) \
	$(NULL)

noinst_LIBRARIES = liblayer23.a
liblayer23_a_SOURCES = \
	apn.c \
	apn_fsm.c \
	gps.c \
	l1ctl.c \
	l1l2_interface.c \
	l1ctl_lapdm_glue.c \
	logging.c \
	ms.c \
	networks.c \
	pkt_acc_proc_fsm.c \
	sap_fsm.c \
	sap_proto.c \
	sap_interface.c \
	settings.c \
	sim.c \
	subscriber.c \
	support.c \
	sysinfo.c \
	utils.c \
	vty.c \
	$(NULL)

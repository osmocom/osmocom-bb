AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOCODING_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(NULL)


noinst_LTLIBRARIES = libl1sched.la

libl1sched_la_SOURCES = \
	sched_lchan_common.c \
	sched_lchan_pdtch.c \
	sched_lchan_desc.c \
	sched_lchan_xcch.c \
	sched_lchan_tchf.c \
	sched_lchan_tchh.c \
	sched_lchan_rach.c \
	sched_lchan_sch.c \
	sched_mframe.c \
	sched_clck.c \
	sched_prim.c \
	sched_trx.c \
	$(NULL)

libl1sched_la_LIBADD = \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOCODING_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(NULL)


bin_PROGRAMS = trxcon

trxcon_SOURCES = \
	l1ctl_server.c \
	l1ctl.c \
	trx_if.c \
	logging.c \
	trxcon.c \
	$(NULL)

trxcon_LDADD = \
	libl1sched.la \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(NULL)

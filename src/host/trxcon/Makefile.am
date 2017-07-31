AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6

# versioning magic
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

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

bin_PROGRAMS = trxcon

trxcon_SOURCES = \
	l1ctl_link.c \
	l1ctl.c \
	trx_if.c \
	logging.c \
	trxcon.c \
	$(NULL)

# Scheduler
trxcon_SOURCES += \
	sched_lchan_common.c \
	sched_lchan_desc.c \
	sched_lchan_xcch.c \
	sched_lchan_rach.c \
	sched_lchan_sch.c \
	sched_mframe.c \
	sched_clck.c \
	sched_trx.c \
	$(NULL)

trxcon_LDADD = \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOCODING_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(NULL)

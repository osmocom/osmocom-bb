AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOVTY_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBOSMOGPRSRLCMAC_CFLAGS) \
	$(LIBGPS_CFLAGS) \
	$(NULL)

LDADD = \
	$(top_builddir)/src/common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOCODEC_LIBS) \
	$(LIBOSMOGPRSRLCMAC_LIBS) \
	$(LIBGPS_LIBS) \
	$(NULL)

bin_PROGRAMS = \
	bcch_scan \
	ccch_scan \
	echo_test \
	cell_log \
	cbch_sniff \
	gsmmap \
	$(NULL)

noinst_HEADERS = \
	bcch_scan.h \
	$(NULL)

bcch_scan_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	app_bcch_scan.c \
	bcch_scan.c \
	$(NULL)

ccch_scan_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	app_ccch_scan.c \
	rslms.c \
	$(NULL)

echo_test_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	app_echo_test.c \
	$(NULL)

cell_log_LDADD = $(LDADD) -lm
cell_log_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	app_cell_log.c \
	cell_log.c \
	geo.c \
	$(NULL)

cbch_sniff_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	app_cbch_sniff.c \
	$(NULL)

gsmmap_LDADD = $(LDADD) -lm
gsmmap_SOURCES = \
	gsmmap.c \
	geo.c \
	locate.c \
	log.c \
	$(NULL)

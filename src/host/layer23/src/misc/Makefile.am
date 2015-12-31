AM_CPPFLAGS = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS) $(LIBGPS_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS) $(LIBOSMOGSM_LIBS) $(LIBOSMOCODEC_LIBS) $(LIBGPS_LIBS)

bin_PROGRAMS = bcch_scan ccch_scan echo_test cell_log cbch_sniff

bcch_scan_SOURCES = ../common/main.c app_bcch_scan.c bcch_scan.c
ccch_scan_SOURCES   = ../common/main.c app_ccch_scan.c rslms.c
echo_test_SOURCES = ../common/main.c app_echo_test.c
cell_log_LDADD = $(LDADD) -lm
cell_log_SOURCES = ../common/main.c app_cell_log.c cell_log.c \
			../../../gsmmap/geo.c
cbch_sniff_SOURCES = ../common/main.c app_cbch_sniff.c

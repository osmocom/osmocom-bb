INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS) $(LIBOSMOGSM_LIBS) $(LIBOSMOCODEC_LIBS)

bin_PROGRAMS = bcch_scan ccch_scan cell_log

bcch_scan_SOURCES = ../common/main.c app_bcch_scan.c bcch_scan.c
ccch_scan_SOURCES   = ../common/main.c app_ccch_scan.c rslms.c xcch.c
cell_log_LDADD = $(LDADD) -lm
cell_log_SOURCES = ../common/main.c app_cell_log.c cell_log.c \
			../../../gsmmap/geo.c

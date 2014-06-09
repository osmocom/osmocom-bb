AM_CFLAGS = -Wall $(all_includes) -I$(top_srcdir)/include $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS) $(LIBSOSMODSP_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS) $(LIBOSMOVTY_LIBS) $(LIBOSMOGSM_LIBS) $(LIBOSMOCODEC_LIBS) $(LIBOSMODSP_LIBS)

bin_PROGRAMS = transceiver-ms

transceiver_ms_SOURCES = main.c l1ctl.c l1ctl_link.c trx.c ../common/logging.c

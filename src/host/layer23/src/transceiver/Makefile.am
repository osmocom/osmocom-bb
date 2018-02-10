AM_CFLAGS = -Wall $(all_includes) -I$(top_srcdir)/include $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS) $(LIBOSMODSP_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS) $(LIBOSMOVTY_LIBS) $(LIBOSMOGSM_LIBS) $(LIBOSMOCODEC_LIBS) $(LIBOSMODSP_LIBS)

bin_PROGRAMS = transceiver

transceiver_SOURCES = main.c l1ctl_link.c l1ctl.c trx.c gmsk.c gsm_ab.c demod.c ../common/logging.c

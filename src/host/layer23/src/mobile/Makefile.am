AM_CPPFLAGS = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall $(LIBOSMOCORE_CFLAGS) $(LIBOSMOGSM_CFLAGS) $(LIBGPS_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS) $(LIBOSMOVTY_LIBS) $(LIBOSMOGSM_LIBS) $(LIBOSMOCODEC_LIBS) $(LIBGPS_LIBS)

noinst_LIBRARIES = libmobile.a
libmobile_a_SOURCES = gsm322.c gsm480_ss.c gsm411_sms.c gsm48_cc.c gsm48_mm.c \
	gsm48_rr.c mnccms.c settings.c subscriber.c support.c \
	transaction.c vty_interface.c voice.c mncc_sock.c

bin_PROGRAMS = mobile

mobile_SOURCES = main.c app_mobile.c
mobile_LDADD = libmobile.a $(LDADD)



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
	$(LIBOSMOGPRSLLC_CFLAGS) \
	$(LIBOSMOGPRSSNDCP_CFLAGS) \
	$(LIBOSMOGPRSGMM_CFLAGS) \
	$(LIBOSMOGPRSSM_CFLAGS) \
	$(NULL)

bin_PROGRAMS = modem

modem_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	app_modem.c \
	gmm.c \
	grr.c \
	llc.c \
	rlcmac.c \
	sm.c \
	sndcp.c \
	vty.c \
	$(NULL)
modem_LDADD = \
	$(top_builddir)/src/common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOGPRSRLCMAC_LIBS) \
	$(LIBOSMOGPRSLLC_LIBS) \
	$(LIBOSMOGPRSSNDCP_LIBS) \
	$(LIBOSMOGPRSGMM_LIBS) \
	$(LIBOSMOGPRSSM_LIBS) \
	$(NULL)

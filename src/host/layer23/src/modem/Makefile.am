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
	$(NULL)

bin_PROGRAMS = modem

modem_SOURCES = \
	$(top_srcdir)/src/common/main.c \
	$(top_srcdir)/src/misc/rslms.c \
	app_modem.c \
	$(NULL)
modem_LDADD = \
	$(top_builddir)/src/common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOGPRSRLCMAC_LIBS) \
	$(NULL)

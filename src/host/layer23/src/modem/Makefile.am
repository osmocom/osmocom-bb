AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOVTY_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBOSMOCODEC_CFLAGS) \
	$(LIBGPS_CFLAGS) \
	$(LIBLUA_CFLAGS) \
	$(NULL)

bin_PROGRAMS = modem

modem_SOURCES = modem_main.c
modem_LDADD = \
	$(top_builddir)/src/common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOCODEC_LIBS) \
	$(LIBGPS_LIBS) \
	$(LIBLUA_LIBS) \
	$(NULL)

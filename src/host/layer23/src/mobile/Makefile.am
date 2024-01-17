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
	$(LIBOSMOCODEC_CFLAGS) \
	$(LIBOSMOGAPK_CFLAGS) \
	$(LIBGPS_CFLAGS) \
	$(LIBLUA_CFLAGS) \
	$(NULL)

noinst_LIBRARIES = libmobile.a
libmobile_a_SOURCES = \
	gsm322.c \
	gsm480_ss.c \
	gsm411_sms.c \
	gsm48_cc.c \
	gsm44068_gcc_bcc.c \
	gsm48_mm.c \
	gsm48_rr.c \
	gsm414.c \
	mnccms.c \
	mncc_sock.c \
	primitives.c \
	tch.c \
	tch_voice.c \
	transaction.c \
	vty_interface.c \
	$(NULL)

bin_PROGRAMS = mobile

mobile_SOURCES = main.c app_mobile.c
mobile_LDADD = \
	libmobile.a \
	$(top_builddir)/src/common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOGPRSRLCMAC_LIBS) \
	$(LIBOSMOGPRSLLC_LIBS) \
	$(LIBOSMOGPRSSNDCP_LIBS) \
	$(LIBOSMOCODEC_LIBS) \
	$(LIBOSMOGAPK_LIBS) \
	$(LIBGPS_LIBS) \
	$(LIBLUA_LIBS) \
	$(NULL)

# lua support
if BUILD_LUA
AM_CPPFLAGS += -DWITH_LUA=1
libmobile_a_SOURCES += script_lua.c
else
libmobile_a_SOURCES += script_nolua.c
endif

# GAPK I/O support
if BUILD_GAPK
AM_CPPFLAGS += -DWITH_GAPK_IO=1
libmobile_a_SOURCES += gapk_io.c
endif

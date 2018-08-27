AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBOSMOGAPK_CFLAGS) \
	$(LIBGPS_CFLAGS) \
	$(LIBLUA_CFLAGS) \
	$(NULL)

LDADD = \
	$(top_srcdir)/src/common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOCODEC_LIBS) \
	$(LIBOSMOGAPK_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBGPS_LIBS) \
	$(LIBLUA_LIBS) \
	$(NULL)

noinst_LIBRARIES = libmobile.a
libmobile_a_SOURCES = \
	vty_interface.c \
	transaction.c \
	subscriber.c \
	primitives.c \
	settings.c \
	support.c \
	$(NULL)

# Protocol related code
libmobile_a_SOURCES += \
	gsm411_sms.c \
	gsm480_ss.c \
	gsm48_cc.c \
	gsm48_mm.c \
	gsm48_rr.c \
	gsm322.c \
	$(NULL)

# MNCC and voice implementation
libmobile_a_SOURCES += \
	mncc_sock.c \
	gapk_io.c \
	mnccms.c \
	voice.c \
	$(NULL)

# LUA support
if BUILD_LUA
AM_CPPFLAGS += -DWITH_LUA=1
libmobile_a_SOURCES += script_lua.c
else
libmobile_a_SOURCES += script_nolua.c
endif

bin_PROGRAMS = mobile
mobile_SOURCES = \
	app_mobile.c \
	main.c \
	$(NULL)

# Static linkage
mobile_LDADD = \
	libmobile.a \
	$(LDADD) \
	$(NULL)

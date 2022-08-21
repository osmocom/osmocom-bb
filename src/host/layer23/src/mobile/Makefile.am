AM_CPPFLAGS = \
	$(all_includes) \
	-I$(top_srcdir)/include \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBGPS_CFLAGS) \
	$(LIBLUA_CFLAGS) \
	$(NULL)

noinst_LIBRARIES = libmobile.a
libmobile_a_SOURCES = \
	gsm322.c \
	gsm480_ss.c \
	gsm411_sms.c \
	gsm48_cc.c \
	gsm48_mm.c \
	gsm48_rr.c \
	gsm414.c \
	mnccms.c \
	mncc_sock.c \
	primitives.c \
	settings.c \
	subscriber.c \
	support.c \
	transaction.c \
	vty_interface.c \
	voice.c \
	$(NULL)

bin_PROGRAMS = mobile

mobile_SOURCES = main.c app_mobile.c
mobile_LDADD = \
	libmobile.a \
	../common/liblayer23.a \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOVTY_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(LIBOSMOCODEC_LIBS) \
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

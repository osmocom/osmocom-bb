AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6
SUBDIRS = tests

AM_CPPFLAGS = \
	$(all_includes) \
	$(NULL)

AM_CFLAGS = \
	-Wall \
	$(LIBOSMOGSM_CFLAGS) \
	$(LIBOSMOCORE_CFLAGS) \
	$(LIBOSMOCODING_CFLAGS) \
	$(NULL)

bin_PROGRAMS = gprsdecode

gprsdecode_SOURCES = \
	gsmtap.c \
	rlcmac.c \
	gprs.c \
	main.c \
	$(NULL)

noinst_HEADERS = \
	l1ctl_proto.h \
	rlcmac.h \
	gsmtap.h \
	gprs.h \
	$(NULL)

gprsdecode_LDADD = \
	$(LIBOSMOCODING_LIBS) \
	$(LIBOSMOCORE_LIBS) \
	$(LIBOSMOGSM_LIBS) \
	$(NULL)

# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=1:1:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -fPIC -Wall

lib_LTLIBRARIES = libosmogsm.la

libosmogsm_la_SOURCES = a5.c rxlev_stat.c tlv_parser.c comp128.c gsm_utils.c \
                        rsl.c gsm48.c gsm48_ie.c gsm0808.c sysinfo.c \
			gprs_cipher_core.c gsm0480.c abis_nm.c gsm0502.c \
			gsm0411_utils.c gsm0411_smc.c \
			lapd_core.c lapdm.c

libosmogsm_la_LDFLAGS = -version-info $(LIBVERSION)
libosmogsm_la_LIBADD = $(top_builddir)/src/libosmocore.la

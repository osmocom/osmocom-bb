# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=3:0:0

INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall ${GCC_FVISIBILITY_HIDDEN}

# FIXME: this should eventually go into a milenage/Makefile.am
noinst_HEADERS = milenage/aes.h milenage/aes_i.h milenage/aes_wrap.h \
		 milenage/common.h milenage/crypto.h milenage/includes.h \
		 milenage/milenage.h

lib_LTLIBRARIES = libosmogsm.la

libosmogsm_la_SOURCES = a5.c rxlev_stat.c tlv_parser.c comp128.c gsm_utils.c \
                        rsl.c gsm48.c gsm48_ie.c gsm0808.c sysinfo.c \
			gprs_cipher_core.c gsm0480.c abis_nm.c gsm0502.c \
			gsm0411_utils.c gsm0411_smc.c gsm0411_smr.c \
			lapd_core.c lapdm.c \
			auth_core.c auth_comp128v1.c auth_milenage.c \
			milenage/aes-encblock.c milenage/aes-internal.c \
			milenage/aes-internal-enc.c milenage/milenage.c gan.c

libosmogsm_la_LDFLAGS = $(LTLDFLAGS_OSMOGSM) -version-info $(LIBVERSION)
libosmogsm_la_LIBADD = $(top_builddir)/src/libosmocore.la

EXTRA_DIST = libosmogsm.map

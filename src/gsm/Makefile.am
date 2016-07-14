# This is _NOT_ the library release version, it's an API version.
# Please read Chapter 6 "Library interface versions" of the libtool documentation before making any modification
LIBVERSION=6:0:1

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include $(TALLOC_CFLAGS)
AM_CFLAGS = -Wall ${GCC_FVISIBILITY_HIDDEN}

# FIXME: this should eventually go into a milenage/Makefile.am
noinst_HEADERS = milenage/aes.h milenage/aes_i.h milenage/aes_wrap.h \
		 milenage/common.h milenage/crypto.h milenage/includes.h \
		 milenage/milenage.h

noinst_LTLIBRARIES = libgsmint.la
lib_LTLIBRARIES = libosmogsm.la

libgsmint_la_SOURCES =  a5.c rxlev_stat.c tlv_parser.c comp128.c comp128v23.c \
			gsm_utils.c rsl.c gsm48.c gsm48_ie.c gsm0808.c sysinfo.c \
			gprs_cipher_core.c gprs_rlc.c gsm0480.c abis_nm.c gsm0502.c \
			gsm0411_utils.c gsm0411_smc.c gsm0411_smr.c \
			lapd_core.c lapdm.c kasumi.c gsm_04_08_gprs.c \
			conv_cs2_gen.c conv_cs3_gen.c conv_xcch_gen.c \
			conv_tch_afs_12_2_gen.c conv_tch_afs_10_2_gen.c \
			conv_tch_afs_7_95_gen.c conv_tch_afs_7_4_gen.c \
			conv_tch_afs_6_7_gen.c conv_tch_afs_5_9_gen.c \
			conv_tch_afs_5_15_gen.c conv_tch_afs_4_75_gen.c \
			auth_core.c auth_comp128v1.c auth_comp128v23.c \
			auth_milenage.c milenage/aes-encblock.c gea.c \
			milenage/aes-internal.c milenage/aes-internal-enc.c \
			milenage/milenage.c gan.c ipa.c gsm0341.c apn.c \
			gsup.c gprs_gea.c
libgsmint_la_LDFLAGS = -no-undefined
libgsmint_la_LIBADD = ../libosmocore.la

libosmogsm_la_SOURCES =
libosmogsm_la_LDFLAGS = $(LTLDFLAGS_OSMOGSM) -version-info $(LIBVERSION) -no-undefined $(TALLOC_LIBS)
libosmogsm_la_LIBADD = libgsmint.la

EXTRA_DIST = libosmogsm.map

conv%gen.c: ../../utils/conv_gen.py
	$(AM_V_GEN)python2 ../../utils/conv_gen.py

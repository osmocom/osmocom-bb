INCLUDES = $(all_includes) -I$(top_srcdir)/include -I$(top_builddir)
AM_CFLAGS=-Wall -fno-strict-aliasing $(LIBOSMOCORE_CFLAGS) $(LIBOSMOVTY_CFLAGS) $(COVERAGE_CFLAGS)
AM_LDFLAGS = $(LIBOSMOCORE_LIBS) $(LIBOSMOGSM_LIBS) $(LIBOSMOVTY_LIBS) $(COVERAGE_LDFLAGS)

noinst_LIBRARIES = libgb.a

libgb_a_SOURCES = gprs_ns.c gprs_ns_frgre.c gprs_ns_vty.c \
		  gprs_bssgp.c gprs_bssgp_util.c gprs_bssgp_vty.c gprs_bssgp_bss.c
#gprs_llc.c gprs_llc_vty.c crc24.c

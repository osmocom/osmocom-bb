INCLUDES = $(all_includes) -I$(top_srcdir)/include
AM_CFLAGS = -Wall $(LIBOSMOCORE_CFLAGS)
LDADD = ../common/liblayer23.a $(LIBOSMOCORE_LIBS)

bin_PROGRAMS = bcch_scan layer23 echo_test

bcch_scan_SOURCES = ../common/main.c app_bcch_scan.c bcch_scan.c
layer23_SOURCES   = ../common/main.c app_phone.c layer3.c rslms.c
echo_test_SOURCES = ../common/main.c app_echo_test.c


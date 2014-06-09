dnl Process this file with autoconf to produce a configure script
AC_INIT

AM_INIT_AUTOMAKE(layer23, 0.0.0)

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
PKG_CHECK_MODULES(LIBOSMOVTY, libosmovty)
PKG_CHECK_MODULES(LIBOSMOGSM, libosmogsm)
PKG_CHECK_MODULES(LIBOSMOCODEC, libosmocodec)
AC_CHECK_LIB(gps, gps_waiting, LIBGPS_CFLAGS=" -D_HAVE_GPSD" LIBGPS_LIBS=" -lgps ",,)
AC_SUBST([LIBGPS_CFLAGS])
AC_SUBST([LIBGPS_LIBS])

dnl Optional transceiver
AC_ARG_ENABLE([transceiver], [AS_HELP_STRING([--enable-transceiver], [Build the transceiver application])],
	[osmo_ac_build_transceiver="$enableval"])
if test "$osmo_ac_build_transceiver" = "yes" ; then
	PKG_CHECK_MODULES(LIBOSMODSP, libosmodsp)
fi
AM_CONDITIONAL(BUILD_TRANSCEIVER, test "x$osmo_ac_build_transceiver" = "xyes")

dnl Optional transceiver_ms
AC_ARG_ENABLE([transceiver-ms], [AS_HELP_STRING([--enable-transceiver-ms], [Build the MS transceiver application])],
	[osmo_ac_build_transceiver_ms="$enableval"])
AM_CONDITIONAL(BUILD_TRANSCEIVER_MS, test "x$osmo_ac_build_transceiver_ms" = "xyes")

dnl checks for header files
AC_HEADER_STDC

dnl Checks for typedefs, structures and compiler characteristics

AC_OUTPUT(
    src/Makefile
    src/common/Makefile
    src/misc/Makefile
    src/mobile/Makefile
    src/transceiver/Makefile
    src/transceiver_ms/Makefile
    include/Makefile
    include/osmocom/Makefile
    include/osmocom/bb/Makefile
    include/osmocom/bb/common/Makefile
    include/osmocom/bb/misc/Makefile
    include/osmocom/bb/mobile/Makefile
    Makefile)
